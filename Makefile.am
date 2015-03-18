INCLUDES= -I/usr/local/include -I${srcdir}/deps/boost_1_55_0 -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/su -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/nta -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/sip -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/msg -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/url -I${srcdir}/deps/sofia-sip/libsofia-sip-ua/tport -I$(srcdir)/deps/redisclient/src/redisclient
AM_LDFLAGS= -L/usr/local/lib 
LDADD= -lboost_thread -lpthread -lssl
bin_PROGRAMS= drachtio timer

drachtio_SOURCES= src/main.cpp src/controller.cpp src/drachtio-config.cpp src/client-controller.cpp src/client.cpp src/drachtio.cpp src/sip-dialog.cpp src/sip-dialog-controller.cpp src/sip-proxy-controller.cpp src/pending-request-controller.cpp src/redis-service.cpp src/timer-queue.cpp src/cdr.cpp src/timer-queue-manager.cpp
drachtio_CPPFLAGS=-D_REENTRANT
drachtio_LDADD= ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_log.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_regex.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_filesystem.a -lpthread -lssl -lcrypto

timer_SOURCES= src/test_timer.cpp src/timer-queue.cpp
timer_CPPFLAGS=-D_REENTRANT -DTEST
timer_LDADD=${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_system.a -lpthread -lssl -lcrypto

if LINUX
drachtio_LDADD += -lrt
timer_LDADD += -lrt
endif

BUILT_SOURCES=${srcdir}/deps/boost_1_55_0/stage/lib/libboost_thread.a ${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a

#clean-local:
#	cd ${srcdir}/deps/boost_1_55_0 && b2 --clean

${srcdir}/deps/boost_1_55_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_log.a ${srcdir}/deps/boost_1_55_0/stage/lib/libboost_regex.a:
	cd ${srcdir}/deps/boost_1_55_0 && ./bootstrap.sh --with-libraries=thread,system,log,regex,filesystem && ./b2 stage

${srcdir}/deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a:
	cd ${srcdir}/deps/sofia-sip && autoreconf -fvi && ./configure && $(MAKE)

