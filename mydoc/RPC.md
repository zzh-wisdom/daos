# 参考资料

内部文档：<https://github.com/daos-stack/daos/blob/master/src/README.md>

## 待完成的任务

### 来自术语表

1. dRPC	DAOS Remote Procedure Call
2. gRPC	gRPC Remote Procedure Calls
3. Mercury	A user-space RPC library that can use libfabrics as a transport
4. CART

### 来自代码文档

主要看 daos_server控制平面、daos_engine数据平面、daos_agent 和 dmg 之间的通信。另外受认证的客户端和dao_server之间也存在通信。

**阅读 DAOS Internal 了解DAOS通信的整体设计。**

1. [server](/src/control/server/README.cn.md)。DAOS 服务器使用 gRPC protocol 来与客户端 gRPC 应用程序通信，并通过 Unix 域套接字与 DAOS I/O 引擎进行交互。

控制平面服务器 （daos_server） 实例将打开 gRPC 通道以**侦听来自控制平面客户端应用程序的请求**。

[server.go](/src/control/server/server.go) 包含主要的设置例程，包括建立 gRPC 服务器和注册 RPC。

包含：

- 控制服务
- 管理服务
- 扇出

control应该同时含有gRPC服务器和dRPC客户端。

2. 有关 dRPC 基础知识以及 Go 和 C 中的低级 API 的更多详细信息，请参阅 [dRPC 文档](/src/control/drpc/README.md)。

3. daos_engine 中含有 dRPC服务器的相关代码

4. [dmg](/src/control/cmd/dmg/README.md)中含有dRPC客户端。 **通信**章节部分

5. [daos_agent](/src/control/cmd/daos_agent/README.md) 客户端通信、服务器通信、功能部分

### 待阅读文档

1. dRPC 文档 [dRPC 文档](/src/control/drpc/README.md)
2. CART 文档 <https://github.com/daos-stack/cart>
3. 

