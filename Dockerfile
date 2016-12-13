FROM lyft/opsbase:d883c38f9da3a29967651e683cb97518c2c484f8
COPY . /code/statsrelay-private
RUN apt-get update -y && \
    apt-get install -y software-properties-common python-software-properties && \
    add-apt-repository -y ppa:ubuntu-toolchain-r/test && \
    apt-get update -y && \
    apt-get install -y g++-4.9 cmake libssl-dev clang-format-3.6 autoconf libtool pkg-config check libcurl4-openssl-dev libev-dev libjansson-dev libpcre3-dev libyaml-dev pkg-config && \
    cd /code/statsrelay-private && \
    rm -f CMakeCache.txt && \
    cmake . && make && make test && make integ && cp statsrelay /usr/bin/statsrelay && \
    apt-get purge -y software-properties-common python-software-properties g++-4.9 cmake libssl-dev clang-format-3.6 autoconf libtool pkg-config check libcurl4-openssl-dev libev-dev libjansson-dev libpcre3-dev libyaml-dev pkg-config && \
    apt-get autoremove -y
