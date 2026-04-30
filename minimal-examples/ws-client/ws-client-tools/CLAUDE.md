# CLAUDE.md
项目文件夹ws-client-tools, 这是一个websocket client工具,封装了websocket的操作供其他代码调用.

## 限制
1.本工具基于libwebsockets v4.3.3实现.
2.测试时放置在libwebsockets minimal-examples\ws-client\下即可正常编译.

## 文件列表
 - ws-utils.h 工具头文件,不依赖于其他文件.定义了调用需要使用的结构体,消息类型,函数接口.
 - ws-utils.c 工具具体实现.
 - ws-utils-main.c 调用测试工具

## 参考
```
cmake .. -DCMAKE_BUILD_TYPE=DEBUG -DLWS_WITH_MINIMAL_EXAMPLES=ON -DLWS_WITH_NETLINK=OFF -DLWS_WITH_IPV6=ON -DLWSWITH_ASYNC_DNS=ON
```

## 代码实现风格
1.在打印日志时,优先使用wsi打印,如不满足可使用context打印.

## 函数实现说明
### WstInit
1.入参为消息类型回调函数, 日志打印回调函数
2.创建全局上下文

### WstConnect
1.如果需要加密 则创建vhost,vhost应该针对某一种服务器, 不需要反复申请.
2.创建连接示例,并保存.

## README.md
1.创建README.md,介绍本工具给调用者.
2.在正式改动代码完成后, 同步更新README.md以保证其最新.
3.在回调函数中返回-1,表示断开连接.也可以调用WstDisconnect断开连接.