# dRPC

dRPC是通过**Unix域套接字**在同一物理系统的**本地进程**之间进行通信的一种方式。尽管**每个侦听dRPC服务器都需要自己的Unix域套接字**，但是在任何给定时间，进程都可以充当客户端、服务器或两者兼而有之。

**如果文件系统中该位置已经存在某些内容，则服务器将无法创建套接字，即使它是套接字的较旧版本(创建套接字的位置是在配置文件中规定好的位置，如果该位置已经存在相同名称的文件，则创建套接字失败)**。可选地，您的应用程序可能希望在创建套接字之前`unlink`该文件系统位置。

dRPC调用由**模块**和**方法标识符**定义。可以将dRPC模块视为相关功能的软件包。 dRPC方法指示服务器要执行的特定功能。如果该方法需要参数输入，则它将被编码(marshalled )在dRPC调用的主体(body)中。服务器将以dRPC响应结构体(dRPC response structure)进行响应，该结构体可能在主体(body)中包含特定于方法的响应（比如返回值）。

DAOS dRPC实现依赖于Protocol Buffers来定义通过dRPC通道传递的结构。
必须在[.proto文件](/src/proto)中定义要通过dRPC作为**调用或响应**的一部分发送的任何**结构体**。

> 这些定义都在`/src/proto`目录下。
> 参考 [README](/src/proto/README.cn.md) 的说明。

## Go API

在Go中，drpc软件包同时包含客户端和服务器功能，概述如下。有关C API的文档，请参见[此处](/src/common/README.cn.md)。

dRPC调用和响应由Protobuf生成的`drpc.Call`和`drpc.Response`结构表示。

### Go Client

dRPC客户端由`drpc.ClientConnection`对象表示。

#### 基本客户工作流程

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

### Go Server

dRPC服务器由`drpc.DomainSocketServer`对象表示。

必须为服务器注册单独的dRPC模块，以便处理该模块的传入dRPC调用。要创建dRPC模块，请创建一个实现`drpc.Module`接口的对象。模块ID必须是唯一的。

#### 基本服务器工作流程

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
