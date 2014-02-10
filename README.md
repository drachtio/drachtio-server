# drachtio-server

[![drachtio logo](http://www.dracht.io/images/definition_only.png)](http://dracht.io/)

drachtio-server is a [SIP](http://www.ietf.org/rfc/rfc3261.txt)-based application-agnostic user agent that is built on the [sofia SIP stack](https://gitorious.org/sofia-sip).  It is designed to provide a high-performance SIP processing server platform that offers its capabilities to client applications over a TCP and JSON message-passing interface.  The accompanying client framework is provided by [drachtio-client](https://github.com/davehorton/drachtio-client).

## Building

Currently, you must build the dependencies by hand.  This will be fixed in a future release

### sofia

```bash
cd ${srcdir}/deps/sofia-sip-1.12.11
./configure CPPFLAGS=-DNDEBUG 
..or..
./configure CPPFLAGS=-DDEBUG CXXFLAGS='-g -O0'
make
make install
```

### boost
```bash
cd ${srcdir}/deps/boost_1_55_0
./bootstrap.sh
./b2 stage install
```

### json spirit 
Requires cmake
```bash
cd ${srcdir}/deps/json-spirit
mkdir build
cd build
cmake ..
make
```

### drachtio
```bash
cd ${srcdir}
autoreconf -fvi
mkdir build/debug
cd build/debug
../../configure CPPFLAGS='-DDEBUG' CXXFLAGS='-g -O0'
make
```
