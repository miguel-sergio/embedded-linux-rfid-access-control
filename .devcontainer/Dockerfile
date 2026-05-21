FROM debian:bookworm

ARG USERNAME=buildroot-user
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN apt-get update && apt-get install -y \
    build-essential \
    libncurses-dev \
    git \
    wget \
    cpio \
    python3 \
    python3-dev \
    rsync \
    bc \
    file \
    unzip \
    diffutils \
    sudo \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

USER $USERNAME