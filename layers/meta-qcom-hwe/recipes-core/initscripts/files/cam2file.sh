#!/bin/sh
FILE="/tmp/capture.yuv"

# 把整条 pipeline 写成一行，格式字符串用双引号
gst-launch-1.0 -e \
qtiqmmfsrc name=camsrc \
! "video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1" \
! filesink location=$FILE &
PID=$!

# 等待文件出现并 >1 MB
while [ ! -f "$FILE" ] || [ "$(stat -c%s "$FILE" 2>/dev/null || echo 0)" -le 1048576 ]; do
    sleep 0.5
done

kill -INT $PID
wait $PID
rm -fr $FILE
echo "camera pipeline prepare ok!"
