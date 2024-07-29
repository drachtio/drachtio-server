MYVERSION=$(shell git describe --always 2>/dev/null || echo 1.0.0)
INCLUDES= -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/su -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/nta \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/sip -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/msg \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/url -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/tport \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/bnf -I${srcdir}/deps/jansson/src \
 -I${srcdir}/deps/hiredis \
 -I${srcdir}/deps/prometheus-cpp/build/include -I/usr/local/include

AM_LDFLAGS= -L/usr/local/lib -L${srcdir}/deps/prometheus-cpp/build/lib
LDADD= -lboost_thread -lpthread -lssl -lcrypto -lz

bin_PROGRAMS= drachtio timer ssltest parser uri_test test_https test_asio_curl

ACLOCAL_AMFLAGS = -I m4

drachtio_SOURCES= src/main.cpp src/controller.cpp src/drachtio-config.cpp \
	src/client-controller.cpp src/client.cpp src/drachtio.cpp src/sip-dialog.cpp \
	src/sip-dialog-controller.cpp src/sip-proxy-controller.cpp src/pending-request-controller.cpp \
	src/timer-queue.cpp src/cdr.cpp src/timer-queue-manager.cpp src/sip-transports.cpp \
	src/request-handler.cpp src/request-router.cpp src/stats-collector.cpp \
	src/invite-in-progress.cpp src/blacklist.cpp src/ua-invalid.cpp
drachtio_CPPFLAGS=-D_REENTRANT -DDRACHTIO_VERSION=\"$(MYVERSION)\" -Wno-error=deprecated-declarations \
	-DBOOST_ALLOW_DEPRECATED_HEADERS -DBOOST_LOG_DYN_LINK -O2 -Wno-stringop-overflow
drachtio_LDADD= ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a \
  ${srcdir}/deps/jansson/src/.libs/libjansson.a \
  ${srcdir}/deps/hiredis/libhiredis.a -lcurl -lpthread -lssl -lcrypto -lz

timer_SOURCES= src/test_timer.cpp src/timer-queue.cpp
timer_CPPFLAGS=-D_REENTRANT -DTEST
timer_LDADD=${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a -lboost_thread \
	-lboost_system -lpthread

parser_SOURCES= src/test_parser.cpp
parser_CPPFLAGS=-D_REENTRANT -DTEST
parser_LDADD=${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a -lboost_thread \
	-lboost_system -lboost_log -lboost_filesystem -lpthread -lssl -lcrypto -lz

uri_test_SOURCES= src/test_uri_parser.cpp
uri_test_CPPFLAGS=-D_REENTRANT -DTEST
uri_test_LDADD=-lboost_thread -lboost_system -lboost_filesystem -lpthread -lz

ssltest_SOURCES= src/ssltest.cpp
ssltest_CPPFLAGS=-D_REENTRANT -DDEBUG -g -O2
ssltest_LDADD= -lssl -lcrypto -lz

test_https_SOURCES= src/test_https.cpp
test_https_CPPFLAGS=-D_REENTRANT -DDEBUG -g -O2
test_https_LDADD= -lssl -lcrypto -lz

test_asio_curl_SOURCES= src/test_asio_curl.cpp
test_asio_curl_CPPFLAGS=-D_REENTRANT -DDEBUG -g -O2
test_asio_curl_LDADD=-lboost_thread -lboost_system -lpthread -lcurl -lssl -lcrypto -lz

if LINUX
drachtio_LDADD += -lboost_thread -lboost_system -lboost_log -lboost_filesystem \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-pull.a \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a \
	-lpthread -lssl -lcrypto -lz -lrt
timer_LDADD += -lrt
parser_LDADD += -lrt
test_https_LDADD += -lrt -lpthread -lboost_system
endif
if OSX
drachtio_LDADD += -L/usr/local/opt/openssl/lib -lboost_thread \
	-lboost_system -lboost_log \
	-lboost_filesystem \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-pull.a  \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a  \
	-lpthread -lssl -lcrypto -lz 
drachtio_CPPFLAGS += -I/usr/local/opt/openssl/include -fvisibility=hidden
test_https_LDADD += -L/usr/local/opt/openssl/lib -lboost_system
test_https_CPPFLAGS += -I/usr/local/opt/openssl/include
endif

if TCMALLOC
drachtio_LDADD += -ltcmalloc
endif

BUILT_SOURCES=${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a \
	${srcdir}/deps/jansson/src/.libs/libjansson.a ${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a

clean-local:

${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a:
	cd ${srcdir}/deps/sofia-sip && ./bootstrap.sh && ./configure CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" --with-glib=no && $(MAKE)

${srcdir}/deps/jansson/src/.libs/libjansson.a:
	cd ${srcdir}/deps/jansson && autoreconf -fi && ./configure && $(MAKE)

${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a ${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-pull.a:
	cd ${srcdir}/deps/prometheus-cpp && mkdir -p build && cd build  && \
	cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_PREFIX=`pwd` && \
	$(MAKE) && make install

${srcdir}/deps/hiredis/libhiredis.a:
	cd ${srcdir}/deps/hiredis && cmake -DBUILD_SHARED_LIBS=OFF -DENABLE_SSL=ON . && $(MAKE)
