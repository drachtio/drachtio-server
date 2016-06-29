MYVERSION=$(shell git describe)
INCLUDES= -I/usr/local/include -I${srcdir}/deps/boost_1_59_0 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/su -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/nta -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/sip -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/msg -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/url -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/tport -I$(srcdir)/deps/redisclient/src/redisclient
AM_LDFLAGS= -L/usr/local/lib 
LDADD= -lboost_thread -lpthread -lssl -lcrypto -lz
bin_PROGRAMS= drachtio timer ssltest

drachtio_SOURCES= src/main.cpp src/controller.cpp src/drachtio-config.cpp src/client-controller.cpp src/client.cpp src/drachtio.cpp src/sip-dialog.cpp src/sip-dialog-controller.cpp src/sip-proxy-controller.cpp src/pending-request-controller.cpp src/redis-service.cpp src/timer-queue.cpp src/cdr.cpp src/timer-queue-manager.cpp
drachtio_CPPFLAGS=-D_REENTRANT -DDRACHTIO_VERSION=\"$(MYVERSION)\"
drachtio_LDADD= ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a -lpthread -lssl -lcrypto -lz

timer_SOURCES= src/test_timer.cpp src/timer-queue.cpp
timer_CPPFLAGS=-D_REENTRANT -DTEST
timer_LDADD=${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_system.a -lpthread

ssltest_SOURCES= src/ssltest.cpp
ssltest_CPPFLAGS=-D_REENTRANT -DDEBUG -g -O0

if LINUX
drachtio_LDADD += ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_log.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_regex.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_filesystem.a -lpthread -lssl -lcrypto -lz -lrt
timer_LDADD += -lrt
endif
if OSX
drachtio_LDADD += -lboost_thread -lboost_filesystem -lboost_system -lboost_regex -lboost_log
drachtio_CPPFLAGS += -DBOOST_LOG_DYN_LINK
endif

BUILT_SOURCES=${srcdir}/deps/boost_1_59_0/stage/lib/libboost_thread.a ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a 

#clean-local:
#	cd ${srcdir}/deps/boost_1_59_0 && b2 --clean

${srcdir}/deps/boost_1_59_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_log.a ${srcdir}/deps/boost_1_59_0/stage/lib/libboost_regex.a:
	cd ${srcdir}/deps/boost_1_59_0 && ./bootstrap.sh --with-libraries=thread,system,log,regex,filesystem && ./b2 cxxflags=-std=c++0x stage

${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a:
	cd ${srcdir}/deps/sofia-sip && ./bootstrap.sh && ./configure --with-glib=no && $(MAKE)

