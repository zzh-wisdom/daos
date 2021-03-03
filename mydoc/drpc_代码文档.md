# drpc的代码实现文档

dRPC是通过**Unix域套接字**在同一物理系统的**本地进程**之间进行通信的一种方式。尽管**每个侦听dRPC服务器都需要自己的Unix域套接字**，但是在任何给定时间，进程都可以充当客户端、服务器或两者兼而有之。

**如果文件系统中该位置已经存在某些内容，则服务器将无法创建套接字，即使它是套接字的较旧版本(创建套接字的位置是在配置文件中规定好的位置，如果该位置已经存在相同名称的文件，则创建套接字失败)**。可选地，您的应用程序可能希望在创建套接字之前`unlink`该文件系统位置。

dRPC调用由**模块**和**方法标识符**定义。可以将dRPC模块视为相关功能的软件包。 dRPC方法指示服务器要执行的特定功能。如果该方法需要参数输入，则它将被编码(marshalled )在dRPC调用的主体(body)中。服务器将以dRPC响应结构体(dRPC response structure)进行响应，该结构体可能在主体(body)中包含特定于方法的响应（比如返回值）。

DAOS dRPC实现依赖于Protocol Buffers来定义通过dRPC通道传递的结构。必须在[.proto文件](/src/proto)中定义要通过dRPC作为**调用或响应**的一部分发送的任何**结构体**。

`drpc`目前仅支持同步调用。

## 代码组成

- 主要代码：src/control/drpc
  - [src/control/drpc/drpc_client.go](/src/control/drpc/drpc_client.go)：实现rpc的客户端stub。
  - [src/control/drpc/drpc_server.go](/src/control/drpc/drpc_server.go)：实现rpc的服务器
  - [src/control/drpc/failure.go](/src/control/drpc/failure.go)：各种故障情况的错误处理
  - [src/control/drpc/module_svc.go](/src/control/drpc/module_svc.go)：实现rpc服务的主要地方，它实现根据传入的原始二进制消息数据、找到具体的模块和方法、执行方法、将结果编码返回的全部过程。
  - [src/control/drpc/modules.go](/src/control/drpc/modules.go)：模块实现，这里实现了4种功能模块。
  - [src/control/drpc/property.go](/src/control/drpc/property.go)：定义一些属性常量，可以忽略。
  - [src/control/drpc/status.go](/src/control/drpc/status.go)：定义了daos的所有状态（错误）代码，可以忽略。
- protoc文件：[src/proto/drpc.proto](/src/proto/drpc.proto)

drpc框架本身使用到的protoc文件很少，就1个。

## protoc内容

定义了两种信息类型：

- `Call`：客户端向服务器发送的消息类型，描述了需要调用哪个模块中的哪个方法，其中的body字段存储了该方法使用的参数值
- `Response`：服务器向客户端发送的消息类型，其中的内容包括调用执行的返回结果以及rpc执行的状态信息。其中特定方法的结果也存储在body字段中。

另外还定义了一个枚举enum类型`Status`，用于描述rpc执行过程中可能出现的各种错误情况。

更加具体的情况参考文件[drpc.proto](/src/proto/drpc.proto)

## drpc框架实现

drpc是daos内部实现和使用的轻量级rpc框架，原理非常简单。

### 客户端实现

客户端的实现非常简单。客户端需要发起drpc调用时，首先通过Unix域套接字和服务器发起连接，连接成功后，就可以不断发送Call消息以调用相应的服务器模块方法。

#### 客户端的使用流程

dRPC客户端由`drpc.ClientConnection`对象表示。具体使用流程如下：

1. 使用dRPC服务器的Unix域套接字的路径创建一个新的**客户端连接**：
    ```
    conn := drpc.NewClientConnection("/var/run/my_socket.sock")
    ```
2. 连接到dRPC服务器：
    ```
    err := conn.Connect()
    ```
3. 创建您的`drpc.Call`并将其发送到服务器：
    ```
    call := drpc.Call{}
    // Set up the Call with module, method, and body
    resp, err := drpc.SendMsg(call)
    ```
    错误提示无法发送`drpc.Call`、或接收到无效的`drpc.Response`。如果没有返回错误，则仍应检查`drpc.Response`的内容，以获取服务器报告的错误。
4. 根据需要发送尽可能多的调用。
5. 完成后关闭连接：
    ```
    conn.Close()
    ```

**注意**：drpc仅支持同步调用，即上述的第3步的`SendMsg`函数是同步执行的，只有等到服务器执行完成并返回结果后，该函数才能返回。由于是本地通信，速度较快，这样的简单实现是可以接受的。

### 服务器实现

服务器需要注意两个要点：

- 一个客户端连接建立一个会话
- 服务器的提供的服务通过模块ID和方法ID来标识

客户端每发起一个连接，服务器就会相应建立一个会话session来处理该连接的所有调用。每个会话都会使用一个单独的**go协程**来监听消息并处理。

在服务器中，模块是通过map来管理的，key是模块ID，value是`Module`接口，如下所示。

```go
type ModuleService struct {
    log     logging.Logger
    modules map[ModuleID]Module
}
```

通过map的管理可以在以后的开发中很方便地添加更多的服务模块。

服务器提供了模块注册的接口，在实现应用程序时，可以根据需要加载部分模块，即将需要的模块添加到map中。

每个模块中都有若干个方法，每个方法通过方法ID标识。虽然只用保持模块内的方法ID唯一即可，但daos为了简单和方便起见，使用了全局唯一的方法ID，且每个模块最多只能含有100个方法，具体是每个模块的起始方法ID按照**100的间隔**分配，这样能更加方便的检查和管理。

```go
const moduleMethodOffset = 100
```

#### 服务器的使用流程

dRPC服务器由`drpc.DomainSocketServer`对象表示，基本使用流程如下：

1. 使用服务器的Unix域套接字创建新的DomainSocketServer：
    ```
    drpcServer, err := drpc.NewDomainSocketServer("/var/run/my_socket.sock")
    ```
2. 注册服务器需要处理的dRPC模块：
    ```
    drpcServer.RegisterRPCModule(&MyExampleModule{})
    drpcServer.RegisterRPCModule(&AnotherExampleModule{})
    ```
3. 启动服务器以启动Goroutine，以开始侦听和处理传入的连接：
   ```
   err = drpc.Start()
   ```
4. 需要关闭服务器时，请关闭用于监听的Goroutine：
   ```
   drpcServer.Shutdown()
   ```

服务器启动后，会自动后台创建Goroutine来接收和处理调用。

### 模块

目前daos实现了4个模块。

```go
const (
    // ModuleSecurityAgent is the dRPC module for security tasks in DAOS    agent
    // ModuleSecurityAgent是dRPC模块，用于 DAOS代理中的安全任务
    ModuleSecurityAgent ModuleID = C.DRPC_MODULE_SEC_AGENT
    // ModuleMgmt is the dRPC module for management service tasks
    // ModuleMgmt是用于管理服务任务的dRPC模块
    ModuleMgmt ModuleID = C.DRPC_MODULE_MGMT
    // ModuleSrv is the dRPC module for tasks relating to server setup
    // ModuleSrv是dRPC模块，用于执行与 服务器设置 有关的任务
    ModuleSrv ModuleID = C.DRPC_MODULE_SRV
    // ModuleSecurity is the dRPC module for security tasks in DAOS server
    // ModuleSecurity是dRPC模块，用于 DAOS服务器中的安全任务
    ModuleSecurity ModuleID = C.DRPC_MODULE_SEC
)
```

而且**这些模块不都是必须的**，程序可以根据需要加载特定的模块。

## 其他

[src/control/server/drpc.go](/src/control/server/drpc.go)

- drpc服务器启动时的准备（设置）工作。
- 如何重试执行rpc调用

服务器启动时，需要清除之前使用的所有daos套接字：

- daos_server.sock
- daos_engine*.sock 数据平面使用的套接字有多个

## 待完成

- [src/control/cmd/daos_agent/start.go](/src/control/cmd/daos_agent/start.go)
- [src/control/server/drpc.go](/src/control/server/drpc.go)
