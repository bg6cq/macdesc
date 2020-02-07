# MAC地址信息查询服务

macdesc 程序基于GPL v3发布。

使用 http://standards-oui.ieee.org/oui.txt 查询 MAC 地址对应的厂商。

特点：

* 仅仅一个进程，占用内存约9MB，启动后不再读写任何文件，每秒钟可以响应超1万次查询
* 使用epoll高效接口，单进程支持超1万并发连接（需要使用ulimit -n 10240设置单进程可打开的文件数）

演示站点（请单击如下URL测试）：

* 查询MAC信息 [http://ip.ustc.edu.cn/mac/00-50:56:9f:0018](http://ip.ustc.edu.cn/mac/00-50:56:9f:0018)

命令行：
```
Usage:
   macdescd [ -d ] [ -f ] [ -6 ] [ -o ouidbfilename ] [ tcp_port ]
        -d debug
        -f fork and do
        -6 support ipv6
	-o ouidbfilename, default is oui.db
        default port is 80
```

## 独立进程运行

```
cd /usr/src
git clone https://github.com/bg6cq/macdesc
cd macdesc
make

sh update

./macdescd -f 90
```

如果需要查看运行的调试输出，可以使用

```
./macdescd -f -d 90
```

上面的90是提供服务的tcp端口，访问 http://server_ip:90/XX:XX:XX:XX:XX:XX 即可返回对应的厂商信息

