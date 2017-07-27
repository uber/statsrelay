#!/bin/bash

# Install a much newer copy of LLVM to run asan with

echo "deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-4.0 main" >> /etc/apt/sources.list
echo "deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-4.0 main" >> /etc/apt/sources.list

wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|sudo apt-key add -

apt-get update

apt-get install -y clang-4.0 clang-4.0-doc llvm-4.0-runtime clang-format-4.0 python-clang-4.0 lld-4.0
