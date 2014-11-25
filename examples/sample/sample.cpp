#include <socket_io_client.hpp>
#include <iostream>

int main(int /*argc*/, char* /*argv*/ []) {

   // websocket++ expects urls to begin with "ws://" not "http://"
   std::string uri = "ws://localhost:8080/";

   try {
      // Create and link handler to websocket++ callbacks.
      // Construct the shared ptr
      socketio::socketio_client_handler_ptr m_client_handler_ptr((new socketio::socketio_client_handler()));
      socketio::socketio_client::connection_ptr con;
      // client endpoint(handler);

      // Set log level. Leave these unset for no logging, or only set a few for selective logging.
      // endpoint.elog().set_level(websocketpp::log::elevel::RERROR);
      // endpoint.elog().set_level(websocketpp::log::elevel::FATAL);
      // endpoint.elog().set_level(websocketpp::log::elevel::WARN);
      // endpoint.alog().set_level(websocketpp::log::alevel::DEVEL);

      // std::string socket_io_uri = handler->perform_handshake(uri);
      // con = endpoint.get_connection(socket_io_uri);

      // The previous two lines can be combined:
      // con = endpoint.get_connection(handler->perform_handshake(uri));

      // endpoint.connect(con);

      // boost::thread t(boost::bind(&client::run, &endpoint, false));

      // // Wait for a sec before sending stuff
      // while (!handler->connected()) {
      //    Sleep(1);
      // }

      // handler->bind_event("example", &socketio_events::example);

      // // After connecting, send a connect message if using an endpoint
      // handler->connect_endpoint("/chat");

      // std::getchar();

      // // Then to connect to another endpoint with the same socket, we call connect again.
      // handler->connect_endpoint("/news");

      // std::getchar();

      // handler->emit("test", "hello!");

      // std::getchar();

      // endpoint.stop(false);
   }
   catch (std::exception& e) {
      std::cerr << "Exception: " << e.what() << std::endl;
      std::getchar();
   }
}