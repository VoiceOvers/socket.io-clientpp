/* socket_io_client.cpp
* Evan Shimizu, June 2012
* websocket++ handler implementing the socket.io protocol.
* https://github.com/LearnBoost/socket.io-spec 
*
* This implementation uses the rapidjson library.
* See examples at https://github.com/kennytm/rapidjson.
*/

#include "socket_io_client.h"
#include <sstream>
#include <boost/tokenizer.hpp>
// Comment this out to disable handshake logging to stdout
#define LOG(x) std::cout << x
//#define LOG(x)

using socketio::socketio_client_handler;

using std::stringstream;
// Event handlers


socketio_client_handler::socketio_client_handler() :
   m_heartbeatActive(false),
   m_connected(false),
   m_con_listener(NULL),
   m_io_listener(NULL),
   m_heartbeatTimeout(0),
   m_network_thread(NULL)
{
   m_client.clear_access_channels(websocketpp::log::alevel::all);
   m_client.set_access_channels(websocketpp::log::alevel::connect);
   m_client.set_access_channels(websocketpp::log::alevel::disconnect);
   m_client.set_access_channels(websocketpp::log::alevel::app);

   // Initialize the Asio transport policy
   m_client.init_asio();

   // Bind the handlers we are using
   using websocketpp::lib::placeholders::_1;
   using websocketpp::lib::bind;
   m_client.set_open_handler(bind(&socketio_client_handler::on_open,this,::_1));
   m_client.set_close_handler(bind(&socketio_client_handler::on_close,this,::_1));
   m_client.set_fail_handler(bind(&socketio_client_handler::on_fail,this,::_1));
   m_client.set_message_handler(bind(&socketio_client_handler::on_message,this,::_1,::_2));
}

socketio_client_handler::~socketio_client_handler()
{
    close();
}

// Websocket++ client handler

void socketio_client_handler::on_fail(connection_hdl con)
{
   stop_heartbeat();
   m_con.reset();
   m_connected = false;

   LOG("Connection failed." << std::endl);
   if(m_con_listener)m_con_listener->on_fail(con);
}

void socketio_client_handler::on_open(connection_hdl con)
{
   // Create the heartbeat timer and use the same io_service as the main event loop.
   m_heartbeatTimer = std::unique_ptr<boost::asio::deadline_timer>(new boost::asio::deadline_timer(m_client.get_io_service(), boost::posix_time::seconds(0)));

   start_heartbeat();
   m_connected = true;

   LOG("Connected." << std::endl);
   if(m_con_listener)m_con_listener->on_open(con);
}

void socketio_client_handler::on_close(connection_hdl con)
{  
   stop_heartbeat();
   m_heartbeatTimer.reset();
   m_connected = false;
   m_con.reset();

   LOG("Client Disconnected." << std::endl);
   if(m_con_listener)m_con_listener->on_close(con);
}

void socketio_client_handler::on_message(connection_hdl con, client_type::message_ptr msg)
{
   // Parse the incoming message according to socket.IO rules
   parse_message(msg->get_payload());
}

// Client Functions
// Note from websocketpp code: methods (except for perform_handshake) will be called
// from outside the io_service.run thread and need to be careful to not touch unsynchronized
// member variables.

std::string socketio_client_handler::perform_handshake(std::string url, std::string socketIoResource)
{
   using namespace boost::asio::ip;
   // Log currently not accessible from this function, outputting to std::cout
   LOG("Parsing websocket uri..." << std::endl);
   websocketpp::uri uo(url);
   m_resource = uo.get_resource();

   // Declare boost io_service
   boost::asio::io_service io_service;

   LOG("Connecting to Server..." << std::endl);

   // Resolve query
   tcp::resolver r(io_service);
   tcp::resolver::query q(uo.get_host(), uo.get_port_str());
   tcp::socket socket(io_service);
   boost::asio::connect(socket, r.resolve(q));

   // Form initial post request.
   boost::asio::streambuf request;
   std::ostream reqstream(&request);

   reqstream << "POST " << socketIoResource << "/1/ HTTP/1.0\r\n";
   reqstream << "Host: " << uo.get_host() << "\r\n";
   reqstream << "Accept: */*\r\n";
   reqstream << "Connection: close\r\n\r\n";

   LOG("Sending Handshake Post Request..." << std::endl);

   // Write request.
   boost::asio::write(socket, request);

   // Receive response
   boost::asio::streambuf response;
   boost::asio::read_until(socket, response, "\r\n");
   // Parse response
   std::istream resp_stream(&response);

   // Extract HTTP version, status, and message.
   std::string httpver;
   unsigned int status;
   std::string status_msg;

   resp_stream >> httpver >> status;
   std::getline(resp_stream, status_msg);

   // Log response
   LOG("Received Response:" << std::endl);
   LOG(httpver << " " << status << std::endl);
   LOG(status_msg << std::endl);

   // Read response headers. Terminated with double newline.
   boost::asio::read_until(socket, response, "\0");

   // Log headers.
   std::string header;
    
   while (std::getline(resp_stream, header) && header[0] != '\r')
   {
      LOG(header << std::endl);
   }

   // Handle errors
   if (!resp_stream || httpver.substr(0, 5) != "HTTP/")
   {
      std::cerr << "Invalid HTTP protocol: " << httpver << std::endl;
      return std::string();
   }
   switch (status)
   {
   case(200):
      LOG("Server accepted connection." << std::endl);
      break;
   case(401):
   case(503):
      std::cerr << "Server rejected client connection" << std::endl;
      return std::string();
   default:
      std::cerr << "Server returned unknown status code: " << status << std::endl;
      
      
   }

   // Get the body components.
   std::string body;

   std::getline(resp_stream, body, '\0');

    boost::char_separator<char> sep(":");
    boost::tokenizer< boost::char_separator<char> > tokens(body, sep);
    std::vector<std::string> matches;
    matches.push_back("");
    for(auto it = tokens.begin();it!=tokens.end();++it)
    {
        matches.push_back(*it);
    }
   if (matches.size()>=5)
   {
      m_sid = matches[1];

      m_heartbeatTimeout = atoi(matches[2].c_str());
      if (m_heartbeatTimeout <= 0) m_heartbeatTimeout = 0;

      m_disconnectTimeout = atoi(matches[3].c_str());

      m_transports = matches[4];
      if (m_transports.find("websocket") == std::string::npos)
      {
         std::cerr << "Server does not support websocket transport: " << m_transports << std::endl;
         return std::string();
      }
   }

   // Log socket.IO info
   LOG(std::endl << "Session ID: " << m_sid << std::endl);
   LOG("Heartbeat Timeout: " << m_heartbeatTimeout << std::endl);
   LOG("Disconnect Timeout: " << m_disconnectTimeout << std::endl);
   LOG("Allowed Transports: " << m_transports << std::endl);

   // Form the complete connection uri. Default transport method is websocket (since we are using websocketpp).
   // If secure websocket connection is desired, replace ws with wss.
   std::stringstream iouri;
   iouri << "ws://" << uo.get_host() << ":" << uo.get_port() << socketIoResource << "/1/websocket/" << m_sid;
   m_socketIoUri = iouri.str();
   return m_socketIoUri;
}

void socketio_client_handler::send(const std::string &msg)
{
   if (m_con.expired())
   {
      std::cerr << "Error: No active session" << std::endl;
      return;
   }
   stringstream ss;
   ss<<"Sent:"<<msg<<std::endl;
   m_client.get_alog().write(log::alevel::app,ss.str());
   m_client.send(m_con,msg,frame::opcode::TEXT);
}

void socketio_client_handler::send(unsigned int type, std::string endpoint, std::string msg, unsigned int id)
{
   // Construct the message.
   // Format: [type]:[id]:[endpoint]:[msg]
   std::stringstream package;
   package << type << ":";
   if (id > 0) package << id;
   package << ":" << endpoint << ":" << msg;

   send(package.str());
}

void socketio_client_handler::connect_endpoint(std::string endpoint)
{
   std::stringstream ss;
   ss<<type_connect<<"::"<<endpoint;
   send(ss.str());
}

void socketio_client_handler::disconnect_endpoint(std::string endpoint)
{
   std::stringstream ss;
   ss<<type_disconnect<<"::"<<endpoint;
   send(ss.str());
}

unsigned int socketio_client_handler::s_global_event_id = 0;

void socketio_client_handler::emit(std::string const& name, Document& args, std::string const& endpoint)
{
   // Add the name to the data being sent.
   Value n;
   n.SetString(name.c_str(), name.length(), args.GetAllocator());
   args.AddMember("name", n, args.GetAllocator());

   // Stringify json
   std::ostringstream outStream;
    outStream.precision(8);
   StreamWriter<std::ostringstream> writer(outStream);
   args.Accept(writer);

   // Extract the message from the stream and format it.
   std::string package(outStream.str());
   send(type_event, endpoint, package.substr(0, package.find('\0')), 0);
}

void socketio_client_handler::emit(std::string const& name, Document& args, std::string const& endpoint, std::function<void (void)> ack)
{
   Value n;
   n.SetString(name.c_str(), name.length(), args.GetAllocator());
   args.AddMember("name", n, args.GetAllocator());

   // Stringify json
   std::ostringstream outStream;
   StreamWriter<std::ostringstream> writer(outStream);
   args.Accept(writer);

   // Extract the message from the stream and format it.
   std::string package(outStream.str());
   m_acks[++s_global_event_id] = ack;
   send(type_event, endpoint, package.substr(0, package.find('\0')), s_global_event_id);
}

void socketio_client_handler::emit(std::string const& name, std::string const& arg0, std::string const& endpoint) {
   Document d;
   d.SetObject();
   Value args;
   args.SetArray();
   args.PushBack(arg0.c_str(), d.GetAllocator());
   d.AddMember("args", args, d.GetAllocator());

   emit(name, d, endpoint);
}

void socketio_client_handler::emit(std::string const& name, std::string const& arg0, std::string const& endpoint, std::function<void (void)> ack) {
   Document d;
   d.SetObject();
   Value args;
   args.SetArray();
   args.PushBack(arg0.c_str(), d.GetAllocator());
   d.AddMember("args", args, d.GetAllocator());

   emit(name, d, endpoint,ack);
}


void socketio_client_handler::message(std::string msg, std::string endpoint)
{
   send(3, endpoint, msg, 0);
}


void socketio_client_handler::message(std::string msg, std::string endpoint, std::function<void (void)>  const& ack)
{
   m_acks[++s_global_event_id] = ack;
   send(3, endpoint, msg, s_global_event_id);
}

void socketio_client_handler::json_message(Document& json, std::string endpoint)
{
   // Stringify json
   std::ostringstream outStream;
   StreamWriter<std::ostringstream> writer(outStream);
   json.Accept(writer);

   // Extract the message from the stream and format it.
   std::string package(outStream.str());
   send(4, endpoint, package.substr(0, package.find('\0')), 0);
}

void socketio_client_handler::json_message(Document& json, std::string endpoint, std::function<void (void)>  const& ack)
{
   m_acks[++s_global_event_id] = ack;
   // Stringify json
   std::ostringstream outStream;
   StreamWriter<std::ostringstream> writer(outStream);
   json.Accept(writer);

   // Extract the message from the stream and format it.
   std::string package(outStream.str());
   send(4, endpoint, package.substr(0, package.find('\0')), s_global_event_id);
}

void socketio_client_handler::close()
{
   if (m_con.expired())
   {
      std::cerr << "Error: No active session" << std::endl;
   }
    else
    {
        send(3, "disconnect", "");
        m_client.close(m_con,close::status::normal,"Ended by user");
    }
    if(m_network_thread)
    {
        m_network_thread->join();
        delete m_network_thread;
        m_network_thread = NULL;
        
    }
}

void socketio_client_handler::start_heartbeat()
{
   // Heartbeat is already active so don't do anything.
   if (m_heartbeatActive) return;

   // Check valid heartbeat wait time.
   if (m_heartbeatTimeout > 0)
   {
      m_heartbeatTimer->expires_at(m_heartbeatTimer->expires_at() + boost::posix_time::seconds(m_heartbeatTimeout));
      m_heartbeatActive = true;
      m_heartbeatTimer->async_wait(boost::bind(&socketio_client_handler::heartbeat, this));
      stringstream ss("Sending heartbeats. Timeout: ");
      ss<< m_heartbeatTimeout << std::endl;
      m_client.get_alog().write(log::alevel::devel,ss.str()) ;
   }
}

void socketio_client_handler::stop_heartbeat()
{
   // Timer is already stopped.
   if (!m_heartbeatActive) return;

   // Stop the heartbeats.
   m_heartbeatActive = false;
   m_heartbeatTimer->cancel();

   m_client.get_alog().write(log::alevel::devel,"Stopped sending heartbeats.\n") ;
}

void socketio_client_handler::send_heartbeat()
{
   std::stringstream ss;
   ss<<type_heartbeat<<"::";
   send(ss.str());
   m_client.get_alog().write(log::alevel::devel,"Sent Heartbeat.\n") ;
}

void socketio_client_handler::heartbeat()
{
   send_heartbeat();

   m_heartbeatTimer->expires_at(m_heartbeatTimer->expires_at() + boost::posix_time::seconds(m_heartbeatTimeout));
   m_heartbeatTimer->async_wait(boost::bind(&socketio_client_handler::heartbeat, this));
}

void socketio_client_handler::parse_message(const std::string &msg)
{
   // Parse response according to socket.IO rules.
   // https://github.com/LearnBoost/socket.io-spec

// const boost::regex expression("([0-8]):([0-9]*):([^:]*)[:]?(.*)");

//    boost::char_separator<char> sep(":");
//    boost::tokenizer< boost::char_separator<char> > tokens(msg, sep);
    
    
    std::vector<std::string> matches;
    matches.push_back("");
    int pos = -1;
    bool remind = true;
    for (int i =0; i<3; ++i) {
        int nextpos = msg.find(':',pos+1);
        
        if(nextpos == std::string::npos)
        {
            matches.push_back(msg.substr(pos+1));
            remind = false;
            break;
        }
        matches.push_back(msg.substr(pos+1,nextpos - pos - 1));
        pos = nextpos;
    }
    
    if (remind) {
        matches.push_back(msg.substr(pos+1));
    }
  
    if(matches.size()>2)
   {
      int type;
      int msgId;
      Document json;

      // Attempt to parse the first match as an int.
      std::stringstream convert(matches[1]);
      if (!(convert >> type)) type = -1;

      // Store second param for parsing as message id. Not every type has this, so if it's missing we just use 0 as the ID.
      std::stringstream convertId(matches[2]);
      if (!(convertId >> msgId)) msgId = 0;

      switch (type)
      {
         // Disconnect
      case (0):
         m_client.get_alog().write(log::alevel::devel, "Received message type 0 (Disconnect)\n") ;
         close();
         break;
         // Connection Acknowledgement
      case (1):
         {
            stringstream ss("Received Message type 1 (Connect ACK): ");
            ss<<msg<<std::endl;
            m_client.get_alog().write(log::alevel::devel,ss.str());
            break;
         }
         // Heartbeat
      case (2):
         {
            m_client.get_alog().write(log::alevel::devel, "Received Message type 2 (Heartbeat)\n") ; 
            send_heartbeat();
            break;
         }
         // Message
      case (3):
         {
            stringstream ss("Received Message type 3 (Message): ");
            ss<<msg<<std::endl;
            m_client.get_alog().write(log::alevel::devel,ss.str());
            on_socketio_message(msgId, matches[3], matches[4]);
            break;
         }
         // JSON Message
      case (4):
         {
            stringstream ss("Received Message type 4 (JSON Message): ");
            ss<<msg<<std::endl;
            m_client.get_alog().write(log::alevel::devel,ss.str());

            // Parse JSON
            if (json.Parse<0>(matches[4].data()).HasParseError())
            {
               m_client.get_elog().write(log::elevel::warn, "Json Parse Error\n") ; 
               return;
            }
            on_socketio_json(msgId, matches[3], json);
            break;
         };
         // Event
      case (5):
         {
            stringstream ss("Received Message type 5 (Event): ");
            ss<<msg<<std::endl;
            m_client.get_alog().write(log::alevel::devel,ss.str());

            // Parse JSON
            if (json.Parse<0>(matches[4].c_str()).HasParseError())
            {
               m_client.get_elog().write(log::elevel::warn, "Json Parse Error\n") ; 
               return;
            }
            if (!json["name"].IsString())
            {
               m_client.get_elog().write(log::elevel::warn, "Json Parse Error\n") ; 
               return;
            }
            on_socketio_event(msgId, matches[3], json["name"].GetString(), json["args"]);
            break;
         }
         // Ack
      case (6):
         {
            m_client.get_alog().write(log::alevel::devel, "Received Message type 6 (ACK)\n") ;
            on_socketio_ack(matches[4]);
            break;
         }
         // Error
      case (7):
         {
            stringstream ss("Received Message type 7 (Error): ");
            ss<<msg<<std::endl;
            m_client.get_alog().write(log::alevel::devel,ss.str());
            on_socketio_error(matches[3], matches[4].substr(0, matches[4].find("+")), matches[4].substr(matches[4].find("+")+1));
            break;
         }
         // Noop
      case (8):
         {
            m_client.get_alog().write(log::alevel::devel, "Received Message type 8 (Noop)\n");
            break;
         }
      default:
         break;
      }
   }
   else
   {
      stringstream ss("Non-Socket.IO message: ");
      ss<<msg<<std::endl;
      m_client.get_elog().write(log::elevel::warn, ss.str());
   }
}

void socketio_client_handler::connect(const std::string& uri)
{
   m_network_thread = new lib::thread(lib::bind(&socketio_client_handler::run_loop,this,uri));//uri lifecycle?
}


void socketio_client_handler::run_loop(const std::string & uri)
{
    try
    {
        std::string io_uri = this->perform_handshake(uri);
        
        
        if(io_uri.size() == 0)
        {
            if(m_con_listener)m_con_listener->on_fail(m_con);//where to kill the thread?
            return;
        }
        lib::error_code ec;
        client_type::connection_ptr con = m_client.get_connection(io_uri, ec);
        if (ec) {
            m_client.get_alog().write(websocketpp::log::alevel::app,
                                      "Get Connection Error: "+ec.message());
            return;
        }
        
        // Grab a handle for this connection so we can talk to it in a thread
        // safe manor after the event loop starts.
        m_con = con->get_handle();
        
        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        m_client.connect(con);
        m_client.run();
        m_client.get_alog().write(websocketpp::log::alevel::devel,
                                  "run loop end");
    }
    catch(std::exception const& e)
    {
        std::cout<<"connect fail:"<<e.what()<<std::endl;
    }
}


void socketio_client_handler::ack(int msg_id,std::string const& ack_reponse)
{
   std::stringstream package;
   package << type_ack << ":"<<msg_id<<"::"<<ack_reponse;

   send(package.str());
}

void socketio_client_handler::on_socketio_proxy(int msg_id,std::function<void(std::string* ack_response)> func)
{
   std::string* p_ack_reponse = NULL;
   if(msg_id > 0)
   {
      p_ack_reponse = new std::string();
   }
   func(p_ack_reponse);
   if(msg_id>0)
   {
      this->ack(msg_id,*p_ack_reponse);
      delete p_ack_reponse;
   }
}

void socketio_client_handler::set_connection_listener(socketio::socketio_client_handler::connection_listener *listener)
{
    m_con_listener = listener;
}

void socketio_client_handler::set_socketio_listener(socketio::socketio_client_handler::socketio_listener *listener)
{
    m_io_listener = listener;
}

// This is where you'd add in behavior to handle the message data for your own app.
void socketio_client_handler::on_socketio_message(int msgId, const std::string& msgEndpoint,const std::string& data)
{
   this->on_socketio_proxy(msgId,[&](std::string* ack_response){
      if(m_io_listener)m_io_listener->on_socketio_message(msgEndpoint,data,ack_response);
   });
}

// This is where you'd add in behavior to handle json messages.
void socketio_client_handler::on_socketio_json(int msgId,const std::string& msgEndpoint, Document& json)
{
   this->on_socketio_proxy(msgId,[&](std::string* ack_response){
      if(m_io_listener)m_io_listener->on_socketio_json(msgEndpoint,json,ack_response);
   });
}

// This is where you'd add in behavior to handle events.
// By default, nothing is done with the endpoint or ID params.
void socketio_client_handler::on_socketio_event(int msgId,const std::string& msgEndpoint,const std::string& name, const Value& args)
{
   this->on_socketio_proxy(msgId,[&](std::string* ack_response){
      if(m_io_listener)m_io_listener->on_socketio_event(msgEndpoint,name,args,ack_response);
   });
}

// This is where you'd add in behavior to handle ack
void socketio_client_handler::on_socketio_ack(const std::string& data)
{
   unsigned int id = atoi(data.c_str());
   
   auto it = m_acks.find(id);
   if(it!=m_acks.end())
   {
      (it->second)();
      m_acks.erase(it);
   }
}

// This is where you'd add in behavior to handle errors
void socketio_client_handler::on_socketio_error(const std::string& endpoint,const std::string& reason,const std::string& advice)
{
   if(m_io_listener)m_io_listener->on_socketio_error(endpoint,reason,advice);
}