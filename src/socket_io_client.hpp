/* socket_io_client.hpp
* Evan Shimizu, June 2012
* websocket++ handler implementing (most of) the socket.io protocol.
* https://github.com/LearnBoost/socket.io-spec 
*
* This implementation uses the rapidjson library.
* See examples at https://github.com/kennytm/rapidjson.
*/

#ifndef __SOCKET_IO_CLIENT_HPP__
#define __SOCKET_IO_CLIENT_HPP__

// #pragma warning(disable:4355) // C4355: this used in base member initializer list
#include <boost/tokenizer.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <rapidjson/document.h>     // rapidjson's DOM-style API
#include <rapidjson/prettywriter.h> // for stringify JSON
#include <rapidjson/filestream.h>
#include <rapidjson/stringwriter.h>

// #undef htonll
// #undef ntohll

#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include <map>
#include <string>
#include <queue>

#define JSON_BUFFER_SIZE 20000

using namespace websocketpp;
using namespace rapidjson;



namespace socketio {

   enum packet_type
   {
      type_disconnect = 0,
      type_connect = 1,
      type_heartbeat = 2,
      type_message = 3,
      type_json = 4,
      type_event = 5,
      type_ack = 6,
      type_error = 7,
      type_noop = 8
   };


   typedef client<config::asio_client> client_type;

   class socketio_client_handler {
   public:
      socketio_client_handler() : m_heartbeatActive(false),
         m_connected(false),
         m_con_listener(NULL),
         m_io_listener(NULL),
         m_heartbeatTimeout(0),
         m_network_thread(NULL)
      {
            // m_client.clear_access_channels(websocketpp::log::alevel::all);
            // m_client.set_access_channels(websocketpp::log::alevel::connect);
            // m_client.set_access_channels(websocketpp::log::alevel::disconnect);
            // m_client.set_access_channels(websocketpp::log::alevel::app);

            // // Initialize the Asio transport policy
            // m_client.init_asio();

            // // Bind the handlers we are using
            // using websocketpp::lib::placeholders::_1;
            // using websocketpp::lib::bind;
            
            // m_client.set_open_handler(bind(&socketio::socketio_client_handler::on_open,this,::_1));
            // m_client.set_close_handler(bind(&socketio::socketio_client_handler::on_close,this,::_1));
            // m_client.set_fail_handler(bind(&socketio::socketio_client_handler::on_fail,this,::_1));
            // m_client.set_message_handler(bind(&socketio::socketio_client_handler::on_message,this,::_1,::_2));
      };

      ~socketio_client_handler() 
      {};
      class connection_listener
      {
         public:
            virtual void on_fail(connection_hdl con) = 0;
            virtual void on_open(connection_hdl con) = 0;
            virtual void on_close(connection_hdl con) = 0;
            virtual ~connection_listener()
            {}
      };


      class socketio_listener
      {
         public:
            virtual void on_socketio_message(const std::string& msgEndpoint,const std::string& data, std::string* ackResponse) {};
            virtual void on_socketio_json(const std::string& msgEndpoint, Document& json,std::string* ackResponse) {};
            virtual void on_socketio_event(const std::string& msgEndpoint,const std::string& name, const Value& args,std::string* ackResponse) {};
            virtual void on_socketio_error(const std::string& endppoint,const std::string& reason,const std::string& advice) {};
            virtual ~socketio_listener()
            {}
      };

      void set_connection_listener(connection_listener *listener);

      void set_socketio_listener(socketio_listener *listener);

      // Client Functions - such as send, etc.

      // Sends a plain string to the endpoint. No special formatting performed to the string.
      void send(const std::string &msg);

      // Allows user to send a custom socket.IO message
      void send(unsigned int type, std::string endpoint, std::string msg, unsigned int id = 0);

      // Signal connection to the desired endpoint. Allows the use of the endpoint once message is successfully sent.
      void connect_endpoint(std::string endpoint);

      // Signal disconnect from specified endpoint.
      void disconnect_endpoint(std::string endpoint);

      // Emulates the emit function from socketIO (type 5) 
      void emit(std::string const& name, Document& args, std::string const& endpoint = "");

      void emit(std::string const& name, Document& args, std::string const& endpoint, std::function<void (void)> ack);

      void emit(std::string const& name, std::string const& arg0, std::string const& endpoint = "");

      void emit(std::string const& name, std::string const& arg0, std::string const& endpoint, std::function<void (void)> ack);

      // Sends a plain message (type 3)
      void message(std::string msg, std::string endpoint = "");

      void message(std::string msg, std::string endpoint, std::function<void (void)>  const& ack);

      // Sends a JSON message (type 4)
      void json_message(Document& json, std::string endpoint = "");

      void json_message(Document& json, std::string endpoint, std::function<void (void)> const& ack);

      void connect(const std::string& uri);

      // Closes the connection
      void close();

      // Heartbeat operations.
      void start_heartbeat();
      void stop_heartbeat();

      std::string getSid() { return m_sid; }
      std::string getResource() { return m_resource; }
      bool connected() { return m_connected; }
   private:

      // Performs a socket.IO handshake
      // https://github.com/LearnBoost/socket.io-spec
      // param - url takes a ws:// address with port number
      // param - socketIoResource is the resource where the server is listening. Defaults to "/socket.io".
      // Returns a socket.IO url for performing the actual connection.
      std::string perform_handshake(std::string url, std::string socketIoResource = "/socket.io");

      void run_loop(const std::string & uri);

      // Sends a heartbeat to the server.
      void send_heartbeat();

      // Called when the heartbeat timer fires.
      void heartbeat();

      // Parses a socket.IO message received
      void parse_message(const std::string &msg);

      void ack(int id, const std::string &ack_response);

      void on_socketio_proxy(int msg_id,std::function<void(std::string* ack_response)> func);

      // Callbacks
      void on_fail(connection_hdl con);
      void on_open(connection_hdl con);
      void on_close(connection_hdl con);
      void on_message(connection_hdl con, client_type::message_ptr msg);

      // Message Parsing callbacks.
      void on_socketio_message(int msgId,const std::string& msgEndpoint,const std::string& data);
      void on_socketio_json(int msgId,const std::string& msgEndpoint, Document& json);
      void on_socketio_event(int msgId,const std::string& msgEndpoint,const std::string& name, const Value& args);
      void on_socketio_ack(const std::string& data);
      void on_socketio_error(const std::string& endppoint,const std::string& reason,const std::string& advice);

      // Connection pointer for client functions.
      connection_hdl m_con;
      client_type m_client;
      // Socket.IO server settings
      std::string m_sid;
      unsigned int m_heartbeatTimeout;
      unsigned int m_disconnectTimeout;
      std::string m_socketIoUri;
      std::string m_resource;
      bool m_connected;

      // Currently we assume websocket as the transport, though you can find others in this string
      std::string m_transports;

      std::map<unsigned int, std::function<void (void)> > m_acks;

      static unsigned int s_global_event_id;

      // If you're using C++11 use the standar library smart pointer
      std::unique_ptr<boost::asio::deadline_timer> m_heartbeatTimer;

      lib::thread *m_network_thread;

      bool m_heartbeatActive;

      connection_listener* m_con_listener;
      socketio_listener *m_io_listener;
   };

   typedef client<config::asio_client> socketio_client;
   typedef boost::shared_ptr<socketio_client_handler> socketio_client_handler_ptr;
}


#endif // __SOCKET_IO_CLIENT_HPP__