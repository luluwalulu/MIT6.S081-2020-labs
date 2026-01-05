当前Issue：
3.测试multi-progress时卡住


已解决Issue：
2.运行nettests时tcpdump -XXnr packets.pcap不再是什么数据包都没有了。
但显示testing ping: panic: acquire
在两个处理错误情况的return之前释放了锁，但依然报错panic: acquire
线索：tcpdump -XXnr packets.pcap显示两个数据包，xv6可以发送，但无法接收
解决：net_rx中还会调用transmit函数，所以调用前先释放锁

1.运行nettests时到testing ping那里直接卡死，且风扇开始狂转。
解决：transmit函数中设置权限位时|=用成了&=。并且TDT用成了TDH。
解决后转为Issue2
