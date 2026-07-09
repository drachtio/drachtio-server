FROM debian:bookworm-slim AS build
RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    g++ \
    gcc \
    git \
    libboost-filesystem-dev \
    libboost-log-dev \
    libboost-system-dev \
    libboost-thread-dev \
    libcurl4-openssl-dev \
    libgoogle-perftools-dev \
    libssl-dev \
    libtool \
    libtool-bin \
    make \
    zlib1g-dev

WORKDIR /usr/local/src/drachtio-server
COPY . .
RUN ./bootstrap.sh
WORKDIR /usr/local/src/drachtio-server/build
ARG MYVERSION=1.0.0
RUN ../configure --enable-tcmalloc=yes CPPFLAGS='-DNDEBUG' CXXFLAGS='-O2'
RUN make -j$(nproc) MYVERSION=${MYVERSION}

FROM debian:bookworm-slim

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    jq \
    libboost-filesystem1.74.0 \
    libboost-log1.74.0 \
    libboost-system1.74.0 \
    libboost-thread1.74.0 \
    libgoogle-perftools4 \
  && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

COPY --from=build /usr/local/src/drachtio-server/build/drachtio /usr/local/bin/
COPY docker.drachtio.conf.xml /etc/drachtio.conf.xml
COPY ./entrypoint.sh /

VOLUME ["/config"]

ENTRYPOINT ["/entrypoint.sh"]

CMD ["drachtio"]