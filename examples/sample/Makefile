CPPFLAGS=-I${BOOST_ROOT}/include -I/Users/Bryce/Development/School/socket.io-clientpp/src \
-I/Users/Bryce/Development/School/socket.io-clientpp/lib/rapidjson/include \
-I/Users/Bryce/Development/School/socket.io-clientpp/lib/websocketpp/src/ \
-I/Users/Bryce/Development/School/socket.io-clientpp/lib/websocketpp/
LDFLAGS=
LDLIBS=-L${BOOST_ROOT}/lib/ -lboost_system -lboost_random -L/usr/lib -lssl -lpthread -lcrypto -lz

all: sample
	install_name_tool -change libboost_system.dylib ${BOOST_ROOT}/lib/libboost_system.dylib sample
	install_name_tool -change libboost_random.dylib ${BOOST_ROOT}/lib/libboost_random.dylib sample

sample: sample.o
	g++ $(LDFLAGS) -o sample sample.o $(LDLIBS) -std=c++11

sample.o: sample.cpp
	g++ $(CPPFLAGS) -c sample.cpp $(LDLIBS) -std=c++11

clean:
	rm -f sample
	rm -f sample.o