FROM debian:bullseye-slim

RUN apt-get update \
  && apt-get -y --quiet --force-yes upgrade \
  && apt-get install -y --no-install-recommends ca-certificates gcc g++ make build-essential cmake git autoconf automake  curl libtool libtool-bin libssl-dev libcurl4-openssl-dev zlib1g-dev libgoogle-perftools-dev \
  && git clone --depth=50 --branch=main https://github.com/drachtio/drachtio-server.git /usr/local/src/drachtio-server \
  && cd /usr/local/src \
  && curl -O http://ftp.gnu.org/gnu/autoconf/autoconf-2.71.tar.gz && tar -xvf autoconf-2.71.tar.gz \
  && cd autoconf-2.71 && ./configure && make && make install \
  && cd /usr/local/src \
  && rm -Rf autoconf-2.71 \
  && cd /usr/local/src/drachtio-server \
  && git submodule update --init --recursive \
  && ./bootstrap.sh \
  && mkdir /usr/local/src/drachtio-server/build  \
  && cd /usr/local/src/drachtio-server/build  \
  && ../configure --enable-tcmalloc=yes CPPFLAGS='-DNDEBUG' CXXFLAGS='-O2' \
  && make \
  && make install \
  && apt-get purge -y --quiet --auto-remove gcc g++ make cmake build-essential git autoconf automake libtool libtool-bin \
  && rm -rf /var/lib/apt/* \
  && rm -rf /var/lib/dpkg/* \
  && rm -rf /var/lib/cache/* \
  && rm -Rf /var/log/* \
  && rm -Rf /var/lib/apt/lists/* \
  && cd /usr/local/src \
  && cp drachtio-server/docker.drachtio.conf.xml /etc/drachtio.conf.xml \
  && cp drachtio-server/entrypoint.sh / \
  && rm -Rf drachtio-server \
  && cd /usr/local/bin \
  && rm -f timer ssltest parser uri_test test_https test_asio_curl

COPY ./entrypoint.sh /

VOLUME ["/config"]

ENTRYPOINT ["/entrypoint.sh"]

CMD ["drachtio"]
