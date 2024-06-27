FROM debian:bookworm-slim

RUN apt-get update \
  && apt-get -y --quiet --force-yes upgrade \
  && apt-get install -y --no-install-recommends ca-certificates gcc g++ make build-essential cmake git autoconf automake  curl libtool libtool-bin libssl-dev libcurl4-openssl-dev zlib1g-dev libgoogle-perftools-dev jq \
  && git clone --depth=50 --branch=main https://github.com/drachtio/drachtio-server.git /usr/local/src/drachtio-server \
  && cd /usr/local/src \
  && curl -O http://ftp.gnu.org/gnu/autoconf/autoconf-2.71.tar.gz && tar -xvf autoconf-2.71.tar.gz \
  && cd autoconf-2.71 && ./configure && make && make install \
  && cd /usr/local/src \
  && rm -Rf autoconf-2.71 \
  && cd /usr/local/src/drachtio-server \
  && git submodule update --init --recursive \
  && ./bootstrap.sh \
  && mkdir build  \
  && cd build  \
  && ../configure --enable-tcmalloc=yes CPPFLAGS='-DNDEBUG' CXXFLAGS='-O2' \
  && make \
  && make install

FROM debian:bullseye-slim as app

RUN apt-get update \
  && apt-get -y --quiet --force-yes upgrade \
  && apt-get install -y --no-install-recommends ca-certificates libssl-dev libcurl4-openssl-dev curl zlib1g-dev iproute2 procps libgoogle-perftools-dev \
  && apt-get clean


COPY --from=builder /usr/local/bin/drachtio /usr/local/bin/
COPY ./entrypoint.sh /
COPY ./docker.drachtio.conf.xml /etc/drachtio.conf.xml

VOLUME ["/config"]
ENTRYPOINT ["/entrypoint.sh"]

CMD ["drachtio"]
