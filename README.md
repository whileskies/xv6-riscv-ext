# xv6-riscv-ext

Xv6 is the teaching operating system of MIT. I will extend it and do some interesting things.

#### Implement Simple TCP

Based on [xv6 net lab](https://pdos.csail.mit.edu/6.828/2020/labs/net.html)

- [x] Send/Recv TCP
- [x] Unix Socket API
- [x] Timer
- [x] mywget: Simple http web downloader
- [x] myhttpd: Simple http server
- [ ] Retransmission

#### How To Run

https://pdos.csail.mit.edu/6.828/2020/tools.html

```shell
make qemu
mywget www.baidu.com >> index.html
myhttpd &

# Open the browser to access 127.0.0.1:2222
```

#### Demo

<img src="https://whileskies-pic.oss-cn-beijing.aliyuncs.com/demo2.png" alt="demo2" style="zoom:67%;" />

#### Reference

[chobits/tapip](https://github.com/chobits/tapip)

[saminiir/level-ip](https://github.com/saminiir/level-ip)

[pandax381/xv6-net](https://github.com/pandax381/xv6-net)

