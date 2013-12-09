INCLUDES= -I/usr/local/include -I${srcdir}/deps/boost_1_52_0 -I${srcdir}/deps/sofia-sip-1.12.11/libsofia-sip-ua/su -I${srcdir}/deps/sofia-sip-1.12.11/libsofia-sip-ua/nta -I${srcdir}/deps/sofia-sip-1.12.11/libsofia-sip-ua/sip -I${srcdir}/deps/sofia-sip-1.12.11/libsofia-sip-ua/msg -I${srcdir}/deps/sofia-sip-1.12.11/libsofia-sip-ua/url -I$(srcdir)/deps/json-spirit
AM_LDFLAGS= -L/usr/local/lib  
LDADD= -lboost_thread -lpthread -lssl
bin_PROGRAMS= drachtio
drachtio_SOURCES= src/main.cpp src/controller.cpp src/drachtio-config.cpp src/client-controller.cpp src/client.cpp src/sofia-msg.cpp src/drachtio.cpp src/sip-msg-matcher.cpp src/sip-dialog.cpp src/sip-dialog-controller.cpp
drachtio_CPPFLAGS=-D_REENTRANT
drachtio_LDADD= ${srcdir}/deps/sofia-sip-1.12.11/libsofia-sip-ua/.libs/libsofia-sip-ua.a ${srcdir}/deps/boost_1_52_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_52_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_52_0/stage/lib/libboost_log.a ${srcdir}/deps/json-spirit/debug/libjson_spirit.a -lpthread -lssl -lcrypto

if LINUX
#drachtio_LDADD += -lrt
endif

#clean-local:
#	cd ${srcdir}/deps/boost_1_52_0 && b2 --clean

${srcdir}/deps/boost_1_52_0/stage/lib/libboost_thread.a ${srcdir}/deps/boost_1_52_0/stage/lib/libboost_system.a ${srcdir}/deps/boost_1_52_0/stage/lib/libboost_log.a:
	cd ${srcdir}/deps/boost_1_52_0 && ./bootstrap.sh ./b2 stage

${srcdir}/deps/sofia-sip-1.12.11/libsofia-sip-ua/.libs/libsofia-sip-ua.a:
	cd ${srcdir}/deps/sofia-sip-1.12.11 && $(MAKE)

${srcdir}/deps/json-spirit/release/libjson_spirit.a:
	cd ${srcdir}/deps/json-spirit/release && $(MAKE)

