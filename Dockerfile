FROM debian:jessie

WORKDIR /usr/local/src

# tar up the drachtio-server directory as below before running this Dockerfile
# or, to build from a checked in version, refer to https://github.com/davehorton/docker-drachtio-server


RUN apt-get update \
  && apt-get -y --quiet --force-yes upgrade \
  && apt-get install -y --no-install-recommends ca-certificates gcc g++ make build-essential git autoconf automake  curl libtool libtool-bin libssl-dev \

ADD drachtio-server.tar.gz .

RUN ./bootstrap.sh \
  && mkdir build && cd $_  \
  && ../configure CPPFLAGS='-DNDEBUG' CXXFLAGS='-O0' \
  && make \
  && make install \
  && apt-get purge -y --quiet --force-yes --auto-remove gcc g++ make build-essential git autoconf automake curl libtool libtool-bin \
  && rm -rf /var/lib/{apt,dpkg,cache,log}/ \
  && rm -Rf /var/log/* \
  && rm -Rf /var/lib/apt/lists/* \
  && cd /usr/local/src \
  && rm -Rf drachtio-server \
  && cd /usr/local/bin \
  && rm -f timer ssltest parser uri_test

COPY ./docker.drachtio.conf.xml /etc/drachtio.conf.xml

ENTRYPOINT ["/entrypoint.sh"]

CMD ["drachtio"]
