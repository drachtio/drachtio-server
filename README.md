# drachtio-server [![Build Status](https://secure.travis-ci.org/davehorton/drachtio-server.png)](http://travis-ci.org/davehorton/drachtio-server) [![NPM version](https://badge.fury.io/js/drachtio.svg)](http://badge.fury.io/js/drachtio-server)

[![Join the chat at https://gitter.im/davehorton/drachtio-server](https://badges.gitter.im/davehorton/drachtio-server.svg)](https://gitter.im/davehorton/drachtio-server?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

[![drachtio logo](http://www.dracht.io/images/definition_only.png)](http://dracht.io/)

drachtio-server is a [SIP](http://www.ietf.org/rfc/rfc3261.txt)-based application-agnostic user agent that is built on the [sofia SIP stack](https://gitorious.org/sofia-sip).  It is designed to provide a high-performance SIP processing server platform that offers its capabilities to client applications over a TCP and JSON message-passing interface.  The accompanying client framework is provided by [drachtio](https://github.com/davehorton/drachtio).

## Building

```
git clone --depth=50 --branch=develop git://github.com/davehorton/drachtio-server.git && cd drachtio-server
sed -i 's/git@github.com:/https:\/\/github.com\//' .gitmodules
git submodule update --init --recursive
autoreconf -fvi 
mkdir build && cd $_
../configure CPPFLAGS='-DNDEBUG'
make
sudo make install
```

> Note: All third-party dependencies can be found under $(srcdir)/deps.  These include boost, the [sofia sip stack](https://github.com/davehorton/sofia-sip) and [redisclient](https://github.com/davehorton/redisclient).  sofia and redisclient are included as git submodules in this project.

## Platform support and dependencies

drachtio-server has undergone at least some level of testing on the following platforms:
* Centos 6.x
* Ubuntu
* Fedora 20
* Linux Mint
* Mac OSX (10.9.2)

The following libraries are required to build:
* gcc and c++ compilers
* libssl-dev
* libtool
* autoconf
* automake

## Installing

The output of the build process is an executable named 'drachtio'.  You can run `sudo make install` to copy it into /usr/local/bin, or you can run the executable directly from the build directory.  If run with no command line parameters, it will look for a configuration file in /etc/drachtio.conf.xml; alternatively you can specify the config file location by starting the executable with the -f option (e.g. `./drachtio -f ../drachtio.conf.xml`).

The server can be run as a daemon process by running with the --daemon command line parameter.

The process can be installed as a Linux init script using the example script that can be found in drachtio-init-script

## Configuration

Process configuration is supplied in an xml configuration which, as described above, by default is expected to be /etc/drachtio.conf.xml but can be specified otherwise via a command line parameter.

The configuration file includes section for the configuring the sip stack, the port to listen on for client connections, and logging.  

It is all fairly self-explanatory; refer to the [sample configuration file](drachtio.conf.xml) for details.

