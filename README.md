# drachtio-server [![Build Status](https://secure.travis-ci.org/davehorton/drachtio-server.png)](http://travis-ci.org/davehorton/drachtio-server)

![drachtio logo](http://davehorton.github.io/drachtio-srf/img/definition-only-cropped.png)

drachtio-server is a [SIP](http://www.ietf.org/rfc/rfc3261.txt) server that is built on the [sofia SIP stack](https://github.com/davehorton/sofia-sip).  It provides a high-performance SIP engine that can be controlled by client applications written in pure Javascript running on [node.js](https://nodejs.org) .  

The nodejs module used to create drachtio applications [can be found here](https://github.com/davehorton/drachtio-srf).

## Docker
A docker image [can be found here](https://hub.docker.com/r/drachtio/drachtio-server).

The `drachtio/drachtio-server:latest` tagged image is kept current with the tip of the `develop` branch, which is probably what you want.

## Ansible
An ansible role can be found [here](https://github.com/davehorton/ansible-role-drachtio) for building drachtio using ansible.

## Building from source

*Please use the develop branch when building from source, as it is most up to date*

> Note: The build requires libcurl, which can be installed on debian as `sudo apt install libcurl4-openssl-dev`. All other third-party dependencies can be found under $(srcdir)/deps.  These include boost and the [sofia sip stack](https://github.com/davehorton/sofia-sip).  sofia is included as git submodules in this project. 

> Note for OSX: Install and tell the compiler to use homebrew's openssl
```
brew install openssl
export LDFLAGS="-L/usr/local/opt/openssl/lib $LDFLAGS"
export CXXFLAGS="-I/usr/local/opt/openssl/include $CXXFLAGS"
export CFLAGS="-I/usr/local/opt/openssl/include $CFLAGS"
```

After installing libcurl, do as follows:
```
git clone --depth=50 --branch=develop git://github.com/davehorton/drachtio-server.git && cd drachtio-server
git submodule update --init --recursive
./bootstrap.sh
mkdir build && cd $_
../configure CPPFLAGS='-DNDEBUG'
make
sudo make install
```

## Platform support and dependencies

drachtio-server has been most heavily deployed on debian jesse (8) but has undergone at least some level of testing on the following platforms:
* Debian 8, 9
* Centos 6.x
* Ubuntu
* Fedora 20
* Linux Mint
* Mac OSX (10.9.2+)

The following libraries are required to build:
* gcc and c++ compilers
* libssl-dev
* libtool
* autoconf
* automake
* zlib1g-dev

## Installing

The output of the build process is an executable named 'drachtio'.  You can run `sudo make install` to copy it into /usr/local/bin, or you can run the executable directly from the build directory.  If run with no command line parameters, it will look for a configuration file in /etc/drachtio.conf.xml; alternatively you can specify the config file location by starting the executable with the -f option (e.g. `./drachtio -f ../drachtio.conf.xml`).

The server can be run as a daemon process by running with the --daemon command line parameter.

To see all of the command line options, run `drachtio -h`.

The process can be installed as a Linux systemd or init script using the example script that can be found in [drachtio.service](drachtio.service) or [drachtio-init-script](drachtio-init-script).

## Configuring

**TL;DR** 

Review the [sample configuration file](drachtio.conf.xml), which is heavily commented and mostly self-explanatory

### Overview

drachtio can be configured either through an xml configuration file (installed by defaults into `/etc/drachtio.conf.xml`) or via command-line parameters.

#### SIP
The most important configuration parameters specify which sip address(es) and protocols to listen on/for.  drachtio can listen on multiple addresses/ports/protocols simultaneously,  Example config file section:
```xml
<drachtio>
  <sip>
    <contacts>
      <contact>sip:172.28.0.1:5060;transport=udp,tcp</contact>
      <contact>sip:172.28.0.1:5080;transport=udp,tcp</contact>
    </contacts>
```
or, via command line:
```bash
drachtio --contact "sip:172.28.0.1:5060;transport=udp,tcp" \
   --contact "sip:172.28.0.1:5080;transport=udp,tcp"
```
Optionally, you can also specify an external ip address to associate with a sip contact, if the server is set up to masquerade or is otherwise assigned a public IP address that it does not know about.  You can also specify the local network CIDR associated with a sip address, which is useful in scenarios where a server is connected to both public and private networks.  See the sample configuration file for more details on this.

#### Admin port
The server listens for TCP connections (e.g. *inbound* connections) from node.js applications on a specified address and port.
```xml
<drachtio>
  <admin port="9022" secret="cymru">127.0.0.1</admin>
```
or
```
drachtio --port 9022  # address defaults to 0.0.0.0
```

#### Logging
Log files can be written to the console, to a file, or to syslog (or any or all of the above simultaneously).  

In a standard installation, log files are written to `/var/log/drachtio` with the current `drachtio.log` found there, and archived logs in the `archive` sub-folder.  drachtio will automically truncate and roll logs based on the parameters specified in the config file.

In a container implementation, console based logging is more useful, and is the default when all arguments are supplied on the command line.  Log levels for both drachtio and the underlying sofia sip stack can be specified:
```xml
<drachtio>
  <logging>
    <sofia-loglevel>3</sofia-loglevel> <!-- 0-9 -->
    <loglevel>info</loglevel> <!-- notice, warning, error, info, debug -->
```
or
```bash
drachtio --loglevel info --sofia-loglevel 3
```

#### Homer integration
drachtio can send encapsulated SIP messages to [Homer](http://www.sipcapture.org/) for reporting
```xml
<drachtio>
  <sip>
    <capture-server port="9060" hep-version="3" id="101">172.28.0.23</capture-server>
```
or
```
drachtio --homer 172.28.0.23 --homer-id 101  # defaults to HEP3 and UDP
```

#### Fail2ban integration

To install fail2ban on a drachtio server, refer to this [ansible role](https://github.com/davehorton/ansible-role-fail2ban-drachtio) which installs and configures fail2ban with a filter for drachtio log files.
