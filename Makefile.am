AM_CXXFLAGS = -std=c++17

MYVERSION=$(shell git describe --always 2>/dev/null || echo 1.0.0)

AM_LDFLAGS= -L/usr/local/lib -L${srcdir}/deps/prometheus-cpp/build/lib
LDADD= -lboost_thread

bin_PROGRAMS= drachtio

ACLOCAL_AMFLAGS = -I m4

drachtio_SOURCES= src/main.cpp src/controller.cpp src/drachtio-config.cpp \
	src/client-controller.cpp src/client.cpp src/drachtio.cpp src/sip-dialog.cpp \
	src/sip-dialog-controller.cpp src/sip-proxy-controller.cpp src/pending-request-controller.cpp \
	src/timer-queue.cpp src/cdr.cpp src/timer-queue-manager.cpp src/sip-transports.cpp \
	src/request-handler.cpp src/request-router.cpp src/stats-collector.cpp \
	src/invite-in-progress.cpp src/blacklist.cpp src/ua-invalid.cpp

drachtio_CPPFLAGS= -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/su -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/nta \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/sip -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/msg \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/url -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/tport \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/bnf -I${srcdir}/deps/jansson/src \
 -I${srcdir}/deps/hiredis \
 -I${srcdir}/deps/prometheus-cpp/build/include -I/usr/local/include \
 -D_REENTRANT -DBOOST_LOG_DYN_LINK -DDRACHTIO_VERSION=\"$(MYVERSION)\" -Wno-error=deprecated-declarations \
 -DBOOST_ALLOW_DEPRECATED_HEADERS -O2

drachtio_LDADD= ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a \
  ${srcdir}/deps/jansson/src/.libs/libjansson.a \
  ${srcdir}/deps/hiredis/libhiredis.a -lcurl -lpthread -lssl -lcrypto -lz

if LINUX
drachtio_CPPFLAGS += -Wno-stringop-overflow
drachtio_LDADD += -lboost_thread -lboost_system -lboost_log -lboost_filesystem \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-pull.a \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a \
	-lrt
endif

if OSX
drachtio_LDADD += -L/opt/homebrew/opt/openssl@3/lib -lboost_thread-mt \
    -lboost_system-mt -lboost_log-mt \
    -lboost_filesystem \
    ${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-pull.a  \
    ${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a
AM_LDFLAGS += -L/opt/homebrew/lib
drachtio_CPPFLAGS += -I/opt/homebrew/opt/openssl@3/include -I/opt/homebrew/include -fvisibility=hidden
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
