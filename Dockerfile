# 使用 Ubuntu 22.04 作为基础镜像
FROM ubuntu:22.04

# 设置环境变量，避免交互式安装中的提示
ENV DEBIAN_FRONTEND=noninteractive

# 更新软件包列表并安装必要的软件
RUN apt-get update && \
    apt-get install -y \
    python3.10 \
    python3-pip \
    libssl-dev \
    openssl \
    git \
    && apt-get clean

# 使用 pip 安装 onnxruntime 和 websockets
RUN pip3 install onnxruntime websockets

# 安装 PyTorch 相关库
RUN pip3 install torch==2.3.1 torchvision==0.18.1 torchaudio==2.3.1 --index-url https://download.pytorch.org/whl/cu121

# 克隆 ASR-2Pass 项目
RUN git clone https://github.com/soloHeroo/ASR2Pass-docker.git /opt/ASR-2Pass

# 设置工作目录
WORKDIR /opt/ASR-2Pass

# 安装项目依赖
RUN pip3 install -r requirements.txt

# 暴露服务端口
EXPOSE 10095

# 启动服务
CMD ["bash", "./run_prepare_server.sh"]
