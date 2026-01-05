当前Issue：
2.运行nettests时tcpdump -XXnr packets.pcap不再是什么数据包都没有了。
但显示testing ping: panic: acquire
在两个处理错误情况的return之前释放了锁，但依然报错panic: acquire

已解决Issue：
1.运行nettests时到testing ping那里直接卡死，且风扇开始狂转。
解决：transmit函数中设置权限位时|=用成了&=。并且TDT用成了TDH。
解决后转为Issue2
