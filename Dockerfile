FROM fedora:32
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y

ENV PATH="$PATH:$HOME/.cargo/bin"
RUN dnf install -y gcc make g++

COPY . /code
WORKDIR /code

RUN $HOME/.cargo/bin/cargo test --release && \
    $HOME/.cargo/bin/cargo build --release
