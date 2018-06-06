
MYVERSION=$(shell git describe)
INCLUDES= -I/usr/local/include -I${srcdir}/deps/boost_1_63_0 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/su -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/nta -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/sip -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/msg -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/url -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/tport -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/bnf -I${srcdir}/deps/jansson-2.10/src
AM_LDFLAGS= -L/usr/local/lib 
LDADD= -lboost_thread -lpthread -lssl -lcrypto -lz
bin_PROGRAMS= drachtio timer ssltest parser uri_test test_https test_asio_curl

drachtio_SOURCES= src/main.cpp src/controller.cpp src/drachtio-config.cpp src/client-controller.cpp src/client.cpp src/drachtio.cpp src/sip-dialog.cpp src/sip-dialog-controller.cpp src/sip-proxy-controller.cpp src/pending-request-controller.cpp src/timer-queue.cpp src/cdr.cpp src/timer-queue-manager.cpp src/sip-transports.cpp src/request-handler.cpp src/request-router.cpp
drachtio_CPPFLAGS=-D_REENTRANT -DDRACHTIO_VERSION=\"$(MYVERSION)\" -std=c++11 -Wno-error=deprecated-declarations
drachtio_LDADD= ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/jansson-2.10/src/.libs/libjansson.a -lcurl -lpthread -lssl -lcrypto -lz

timer_SOURCES= src/test_timer.cpp src/timer-queue.cpp
timer_CPPFLAGS=-D_REENTRANT -DTEST
timer_LDADD=${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_system.a -lpthread

parser_SOURCES= src/test_parser.cpp
parser_CPPFLAGS=-D_REENTRANT -DTEST
parser_LDADD=${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_log.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_regex.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_filesystem.a -lpthread -lssl -lcrypto -lz

uri_test_SOURCES= src/test_uri_parser.cpp
uri_test_CPPFLAGS=-D_REENTRANT -DTEST
uri_test_LDADD=${srcdir}/deps/boost_1_63_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_regex.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_filesystem.a -lpthread -lz

ssltest_SOURCES= src/ssltest.cpp
ssltest_CPPFLAGS=-D_REENTRANT -DDEBUG -g -O0
ssltest_LDADD= -lssl -lcrypto -lz

test_https_SOURCES= src/test_https.cpp
test_https_CPPFLAGS=-D_REENTRANT -DDEBUG -g -O0
test_https_LDADD= -lssl -lcrypto -lz

test_asio_curl_SOURCES= src/test_asio_curl.cpp
ttest_asio_curl_CPPFLAGS=-D_REENTRANT -DDEBUG -g -O0
test_asio_curl_LDADD= ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_system.a -lpthread -lcurl -lssl -lcrypto -lz


if LINUX
drachtio_LDADD += ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_log.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_regex.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_filesystem.a -lpthread -lssl -lcrypto -lz -lrt
timer_LDADD += -lrt
parser_LDADD += -lrt
test_https_LDADD += -lrt -lpthread ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_system.a
endif
if OSX
drachtio_LDADD += -L/usr/local/opt/openssl/lib ${srcdir}/deps/boost_1_63_0/stage/lib/libboost* -lpthread -lssl -lcrypto -lz 
drachtio_CPPFLAGS += -DBOOST_LOG_DYN_LINK -I/usr/local/opt/openssl/include
test_https_LDADD += ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_system.a -L/usr/local/opt/openssl/lib
test_https_CPPFLAGS += -DBOOST_LOG_DYN_LINK -I/usr/local/opt/openssl/include
endif

BUILT_SOURCES=${srcdir}/deps/boost_1_63_0/stage/lib/libboost_thread.a ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/jansson-2.10/src/.libs/libjansson.a

clean-local:
	cd ${srcdir}/deps/boost_1_63_0 && ./bootstrap.sh && ./b2 clean
	cd ${srcdir}/deps/sofia-sip && make clean  
	cd ${srcdir}/deps/jansson-2.10 && make clean  


${srcdir}/deps/boost_1_63_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_log.a ${srcdir}/deps/boost_1_63_0/stage/lib/libboost_regex.a:
	cd ${srcdir}/deps/boost_1_63_0 && ./bootstrap.sh --with-libraries=thread,system,log,regex,filesystem && ./b2 stage

${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a:
	cd ${srcdir}/deps/sofia-sip && ./bootstrap.sh && ./configure --with-glib=no && $(MAKE)

${srcdir}/deps/jansson-2.10/src/.libs/libjansson.a:
	cd ${srcdir}/deps/jansson-2.10 && autoreconf -fi && ./configure && $(MAKE)



