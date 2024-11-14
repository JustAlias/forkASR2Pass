#!/bin/bash

# 如果是第一次启动，运行初始化脚本
if [ ! -f "/opt/ASR-2Pass/.initialized" ]; then
    echo "Running initial setup..."
    bash /opt/ASR-2Pass/run_prepare_server.sh
    touch /opt/ASR-2Pass/.initialized
fi

# 启动主要服务
echo "Starting main services..."
bash /opt/ASR-2Pass/run_server_2pass.sh &
python3 /opt/ASR-2Pass/html/h5Server.py
