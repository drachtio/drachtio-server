MYVERSION=$(shell git describe --always)
AM_CPPFLAGS= -I${srcdir}/deps/boost_1_78_0 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/su -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/nta \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/sip -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/msg \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/url -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/tport \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/bnf -I${srcdir}/deps/jansson-2.10/src \
 -I${srcdir}/deps//cpp-bredis/include \
 -I${srcdir}/deps/prometheus-cpp/build/include -I/usr/local/include
AM_LDFLAGS= -L/usr/local/lib -L${srcdir}/deps/prometheus-cpp/build/lib -L${srcdir}/deps/boost_1_78_0/stage/lib
LDADD= -lboost_thread -lpthread -lssl -lcrypto -lz
bin_PROGRAMS= drachtio

ACLOCAL_AMFLAGS = -I m4

drachtio_SOURCES= src/main.cpp src/controller.cpp src/drachtio-config.cpp \
	src/client-controller.cpp src/client.cpp src/drachtio.cpp src/sip-dialog.cpp \
	src/sip-dialog-controller.cpp src/sip-proxy-controller.cpp src/pending-request-controller.cpp \
	src/timer-queue.cpp src/cdr.cpp src/timer-queue-manager.cpp src/sip-transports.cpp \
	src/request-handler.cpp src/request-router.cpp src/stats-collector.cpp \
	src/invite-in-progress.cpp src/blacklist.cpp
drachtio_CPPFLAGS=-I${srcdir}/deps/boost_1_78_0 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/su -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/nta \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/sip -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/msg \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/url -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/tport \
 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/bnf -I${srcdir}/deps/jansson-2.10/src \
 -I${srcdir}/deps//cpp-bredis/include \
 -I${srcdir}/deps/prometheus-cpp/build/include -I/usr/local/include -D_REENTRANT -DDRACHTIO_VERSION=\"$(MYVERSION)\" -Wno-error=deprecated-declarations \
	-DBOOST_ALLOW_DEPRECATED_HEADERS 
drachtio_LDADD= ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/jansson-2.10/src/.libs/libjansson.a -lcurl -lpthread -lssl -lcrypto -lz

if LINUX
drachtio_LDADD += ${srcdir}/deps/boost_1_78_0/stage/lib/libboost_thread.a \
	${srcdir}/deps/boost_1_78_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_78_0/stage/lib/libboost_log.a \
	${srcdir}/deps/boost_1_78_0/stage/lib/libboost_filesystem.a \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-pull.a \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a \
	-lpthread -lssl -lcrypto -lz -lrt
make_BOOST_CMD = cd ${srcdir}/deps/boost_1_78_0 && ./bootstrap.sh --with-libraries=thread,system,log,filesystem && ./b2 link=static variant=release stage
endif
if OSX
drachtio_LDADD += -L/usr/local/opt/openssl/lib ${srcdir}/deps/boost_1_78_0/stage/lib/libboost_thread.a \
	${srcdir}/deps/boost_1_78_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_78_0/stage/lib/libboost_log.a \
	${srcdir}/deps/boost_1_78_0/stage/lib/libboost_filesystem.a \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-pull.a  \
	${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a  \
	-lpthread -lssl -lcrypto -lz 
drachtio_CPPFLAGS += -I/usr/local/opt/openssl/include -fvisibility=hidden
make_BOOST_CMD = cd ${srcdir}/deps/boost_1_78_0 && ./bootstrap.sh --with-toolset=clang --with-libraries=thread,system,log,filesystem && ./b2 cxxflags=-std=c++14 link=static variant=release stage && rm -f ./stage/lib/*.dylib
endif

BUILT_SOURCES=${srcdir}/deps/boost_1_78_0/stage/lib/libboost_thread.a ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a \
	${srcdir}/deps/jansson-2.10/src/.libs/libjansson.a ${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a

clean-local:
	cd ${srcdir}/deps/boost_1_78_0 && ./bootstrap.sh && ./b2 clean
	cd ${srcdir}/deps/sofia-sip && make clean  
	cd ${srcdir}/deps/jansson-2.10 && make clean  

${srcdir}/deps/boost_1_78_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_78_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_78_0/stage/lib/libboost_filesystem.a ${srcdir}/deps/boost_1_78_0/stage/lib/libboost_log.a:
	$(make_BOOST_CMD)

${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a:
	cd ${srcdir}/deps/sofia-sip && ./bootstrap.sh && ./configure --with-glib=no && $(MAKE)

${srcdir}/deps/jansson-2.10/src/.libs/libjansson.a:
	cd ${srcdir}/deps/jansson-2.10 && autoreconf -fi && ./configure && $(MAKE)

${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-core.a ${srcdir}/deps/prometheus-cpp/build/lib/libprometheus-cpp-pull.a:
	cd ${srcdir}/deps/prometheus-cpp && mkdir -p build && cd build  && \
	cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_PREFIX=`pwd` && \
	$(MAKE) && make install



