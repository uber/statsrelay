FROM ubuntu:trusty
RUN mkdir /code
COPY ci/llvm.sh /code
RUN apt-get -y update && apt-get install -y wget cmake gcc g++ libev-dev libjansson-dev libpcre3-dev && bash /code/llvm.sh && rm -rf /var/lib/apt/lists/*
COPY . /code
WORKDIR /code
RUN mkdir build && cd build && cmake .. && make -j 4
RUN mkdir build-asan && cd build-asan && CC=clang-4.0 CFLAGS=-fsanitize=address LDFLAGS=-fsanitize=address  LD=clang-4.0 cmake .. && make VERBOSE=1 -j 20

