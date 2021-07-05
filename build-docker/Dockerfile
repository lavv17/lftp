FROM ubuntu

RUN apt-get update        \
    && apt-get install -y \
        libncurses5-dev   \
        libreadline-dev   \
        openssl           \
        libgnutls-dev     \
        libexpat1-dev     \
        zlib1g-dev        \
        build-essential   \
        automake          \
        autoconf          \
        autopoint         \
        gperf             \
        clang             \
        libtool           \
        byacc             \
        flex              \
        git

COPY . /usr/src/lftp

RUN cd /usr/src/lftp      \
    && export CXX=clang++ \
    && export CC=clang    \
    && ./bootstrap        \
    && ./configure        \
    && make               \
    && make install
