# DAOS 数据平面（又名 daos_engine）

## 1. 模块接口

I/O 服务器支持允许按需加载服务器端代码的模块接口。每个模块实际上是一个由 I/O 服务器通过 dlopen 动态加载的库。模块和 I/O 服务器之间的接口在数据结构`dss_module`定义。

每个模块应指定：

- 一个模块名称
- 一个来自daos_module_id的模块标识
- 一个功能feature位图
- 一个模块初始化和finalize（定稿）函数

此外，模块可以选择配置：

- 一个设置和清理函数（整个堆栈启动并运行时将调用）
- CART RPC handlers
- dRPC handlers

## 2. 线程模型和 Argobot 集成

I/O 服务器是一个多线程进程，使用 Argobots 进行非阻塞处理。

默认情况下，每个目标创建一个主 xstream，并且不创建任何卸载的 xstream。可以通过命令行参数配置实际的daos_engine xstream 的个数。此外，还创建了一个额外的 xstream 来处理传入的元数据请求。每个 xstream 绑定到特定的 CPU 内核。主 xstream 是接收来自客户端和其他服务器的传入目标请求的 xstream。特定 ULT（用户级线程） 已开始在网络和 NVMe I/O 操作上取得进展。

## 3. 线程的本地存储 （TLS）

每个 xstream 分配可通过 `dss_tls_get（）` 函数访问的私有存储。注册时，每个模块可以指定具有特定数据结构大小的模块密钥，该数据结构将由 TLS 中的每个 xstream 分配。`dss_module_key_get（）` 函数将返回特定注册模块密钥的数据结构。

## 4. 转换变量Incast Variable集成

DAOS 使用 IV（incast variable）在单个 IV 命名空间（组织为树）下的服务器之间共享值和状态。树根称为 IV 领导，服务器可以是树叶，也可以是非叶。每台服务器维护自己的 IV 缓存。在获取fetch过程中，如果本地缓存无法满足请求，它将请求转发到其父节点，直到到达根节点（IV 领导）。至于更新update，**它首先更新其本地缓存，然后转发到其父节点，直到到达根节点，然后将更改传播到所有其他服务器**。IV 命名空间是归属于每个池的，在池连接期间创建，并在池断开连接期间销毁。要使用 IV，每个用户都需要在 IV 命名空间下注册自身以获得标识，然后用户将使用此 ID 在 IV 命名空间下获取或更新自己的 IV 值。

## 5. dRPC 服务器

I/O 服务器包括一个 dRPC 服务器，该服务器侦听给定 Unix 域套接字上的活动。有关 dRPC 基础知识以及 Go 和 C 中的低级 API 的更多详细信息，请参阅 [dRPC 文档](/src/control/drpc/README.md)。

dRPC 服务器定期轮询传入的客户端连接和请求。它可以通过`struct drpc_progress_context` 对象同时处理多个客户端连接，该struct对象管理`struct drpc` 对象以侦听套接字和任何活动客户端连接。

服务器循环在 xstream 0 中自己的用户级线程 （ULT） 里运行。dRPC 套接字设置为非阻塞，轮询使用0超时，这允许服务器在 ULT 中运行，而不是在它自己的 xstream 中运行。预计该通道流量相对较低。

### 5.1. dRPC Progress

`drpc_progress`表示 dRPC 服务器循环的一次迭代。工作流如下： 

1. 使用特定的的超时时间不断轮询侦听的套接字和任何打开的客户端连接。
2. 如果在客户端连接上看到任何活动：
    1. 如果数据已传入：调用 `drpc_recv` 处理传入的数据。
    2. 如果客户端已断开连接或连接被破坏：释放 `struct drpc` 对象并将其从 `drpc_progress_context`删除。
3. 如果在侦听器上看到任何活动：
    1. 如果一个新连接已到来：调用`drpc_accept`，并将新的`struct drpc`对象添加到 `drpc_progress_context`中的客户端连接列表中。
    2. 如果出现错误：返回 `-DER_MISC` 给调用方。这会导致在 I/O 服务器中记录错误，但不会中断 dRPC 服务器循环。在侦听器上收到错误是unexpected的。
4. 如果未看到任何活动，则返回 `-DER_TIMEDOUT` 给调用者。这纯粹是出于调试目的。实际上，I/O 服务器会忽略此错误代码，因为缺少活动实际上不是错误情况。

### dRPC 处理程序注册

单个 DAOS 模块可以通过注册一个或多个 **dRPC 模块 ID** 的处理程序函数来实现 dRPC 消息的处理。

注册处理程序非常简单。在`dss_server_module`的字段`sm_drpc_handlers`中，静态分配`struct dss_drpc_handler`数组，并将数组的最后一个元素归零，以指示数组的末尾。将字段设置为 NULL 表示没有东西需要注册。当 I/O 服务器加载 DAOS 模块时，它将自动注册所有 dRPC 处理程序。

注意：dRPC 模块 ID 与 DAOS 模块 ID 不一样。这是因为给定的 DAOS 模块可能需要注册多个 dRPC 模块 ID，具体取决于它涵盖的功能。**dRPC 模块 ID 必须是系统范围中唯一的**，并列在中央头文件中：`src/include/daos/drpc_modules.h`。

dRPC 服务器使用函数`drpc_hdlr_process_msg`处理传入消息。此函数检查传入消息的模块 ID、搜索处理程序、在找到处理程序时执行处理程序、并返回`Drpc__Response`。如果未找到，它将生成自己的`Drpc__Response`，以指示模块 ID 未注册。
