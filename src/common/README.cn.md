# DAOS Common Libraries

外部共享库中提供了**所有DAOS组件**之间共享的通用功能和基础结构。这包括以下功能：

- 哈希和校验和例程
- 事件和事件队列以支持非阻塞操作
- 日志记录和调试的基础设施
- 锁原语
- 网络传输

## Task Scheduler Engine (TSE)

The TSE is a generic library to create generic tasks with function callbacks,
optionally add dependencies between those tasks, and schedule them in an engine
that is progressed to execute those tasks in an order determined by a dependency
graph in which they were inserted in. The task dependency graph is the integral
part of the scheduler to allow users to create several tasks and progress them
in a non-blocking manner.

The TSE is not DAOS specific, but used to be part of the DAOS core and was later
extracted into the common src as a standalone API. The API is generic and allows
creation of tasks in an engine without any DAOS specific functionality. The DAOS
library does provide a task API that is built on top of the TSE. For more
information on that see [here](/src/client/api/README.md). Furthermore, DAOS
uses the TSE internally to track and progress all API tasks that are associated
with the API event and, in some cases, to schedule several inflight "child"
tasks corresponding to a single API task and add a dependency on that task to
track all those inflight "child" tasks. An example of that would be the Array
API in the DAOS library and the object update with multiple replicas.

### Scheduler API

The scheduler API allows a user to create a generic scheduler and add tasks to
it. At the time of scheduler creation, the user can register a completion
callback to be called when the scheduler is finalized.

The tasks that are added to the scheduler do not progress on their own. There
has to be explicit calls to a progress function (daos_sched_progress) on the
scheduler to make progress on the tasks in the engine. This progress function
can be called by the user occasionally in their program, or a single thread can
be forked that calls the progress function repeatedly.

### Task API

The task API allows the creation of tasks with generic body functions and adding
them to a scheduler. Once a task is created within a scheduler, it will not be
actually scheduled to run without an explicit call from the user to the task
schedule function, unless it's part of a task dependency graph where in this
case the explicit schedule call is required only to the first task in the
graph. After creating the task, the user can register any number of dependencies
for the task that would be required to complete before the task can be scheduled
to run. In addition, the user will be able to register preparation and
completion callback on the task:

- Preparation Callbacks are executed when the task is ready to run but has not
  been executed yet, meaning the dependencies that the task was created with are
  done and the scheduler is ready to schedule the task. This is useful when the
  task to be scheduled needs information that is not available at the time of
  task creation but will be available after the dependencies of the task
  complete; for example setting some input parameters for the task body
  function.

- Completion Callbacks are executed when the task is finished executing and the
  user needs to do more work or handling when that happens. An example where
  this would be useful is setting the completion of a higher level event or
  request that is built on top of the TSE, or to track error status of multiple
  tasks in a dependency list.

Several other functionality on the task API exists to support:

- setting some private data on the task itself that can be queried.

- pushing and popping data on/from task stack space without data copy

- generic task lists

More detail about that functionality can be found in the TSE header in the DAOS
code [here](/src/include/daos/tse.h).

## dRPC C API

有关dRPC概念和相应的Go API的综合概述，请参见[这里](/src/control/drpc/README.md)。

在C API中，有效的dRPC连接由指向上下文对象（`struct drpc`）的指针表示。上下文提供了通过Unix域套接字进行通信所需的**所有状态信息**。

默认情况下，上下文以对其进行的引用开头。您可以使用`drpc_add_ref()`添加对给定上下文的引用。完成上下文后，应使用`drpc_close()`释放对象。释放最后一个引用后，将释放该对象。

dRPC调用和响应由Protobuf生成的结构`Drpc__Cal`l和`Drpc__Response`表示。

### C Client

连接到有效的Unix域套接字会返回dRPC上下文，该上下文可用于执行任意数量的dRPC调用，真正执行调用的是建立该套接字的服务器。

**注意**：当前仅支持同步调用（使用标志`R_SYNC`）。异步调用会收到即时响应，但不会被真正处理和执行。

#### 基本客户端工作流程 Basic Client Workflow

1. 打开与服务器的Unix域套接字的连接：
    ```
    struct drpc *ctx;
    rc = drpc_connect("/var/run/my_socket.sock", &ctx);
    ```
2. 发送dRPC调用：
    ```
    Drpc__Call *call;
    /* Alloc and set up your Drpc__Call */
    Drpc__Response *resp = NULL; /* Response will be allocated by drpc_call */
    int result = drpc_call(ctx, R_SYNC, call, &resp);
    ```
    从`drpc_call()`返回的错误代码表示消息无法发送、或者没有响应。如果`drpc_call（）`成功返回，则仍然需要检查响应的内容是否有服务器返回的错误。
3. 根据需要发送尽可能多的dRPC调用。
4. 完成连接后，将其关闭：`drpc_close(ctx)`
    **注意**：调用`drpc_close（）`之后，dRPC上下文指针已被释放并且不再有效。

### C Server

dRPC服务器设置Unix域套接字，并开始在其上侦听客户端连接。通常，这意味着创建侦听dRPC的上下文以检测任何传入连接。然后，当客户端连接时，将为该特定会话生成新的dRPC上下文。会话上下文是实际发送和接收数据的上下文。可以同时打开多个会话上下文。

套接字始终设置为非阻塞套接字，因此在符合POSIX的系统上，有必要使用诸如`poll()`或`select()`之类的系统调用来轮询上下文的文件描述符（`ctx-> comm-> fd`）上的活动。这不仅适用于侦听dRPC上下文，而且还适用于为特定客户端**会话**生成的任何dRPC上下文（会话上下文）。

**服务器流**依赖于自定义处理程序函数，该函数的工作是适当地调度传入的`Drpc__Call`消息。处理函数应检查模块和方法ID，确保执行了所需的方法，并根据结果创建`Drpc__Response`。

#### Basic Server Workflow

1. 在给定路径上设置Unix域套接字，并使用自定义处理函数创建**监听上下文**：
    ```
    void my_handler(Drpc__Call *call, Drpc__Response **resp) {
        /* Handle the message based on module/method IDs */
    }
    ...
    struct drpc *listener_ctx = drpc_listen("/var/run/drpc_socket.sock",
                                            my_handler);
    ```
2. 轮询侦听器上下文的文件描述符（`listener_ctx->comm->fd`）。
3. 在传入活动中，接受连接：
    ```
    struct drpc *session_ctx = drpc_accept(listener_ctx);
    ```
    这将为**特定客户端**创建一个会话上下文。该客户端的所有通信都将通过会话上下文进行。
4. 轮询会话上下文的文件描述符（`session_ctx->comm->fd`）以获取传入数据。
5. 关于传入的数据，收到以下消息：
    ```
    Drpc__Call *incoming;
    int result = drpc_recv_call(session_ctx, &incoming);
    if (result != 0) {
        /* process errors */
    }
    ```
    这会将输入的数据解组到一个`Drpc__Call`中。如果数据不是一个`Drpc__Call`，它返回一个错误。
6. 分配一个`Drpc__Response`并将其传递到会话处理程序中。
    ```
    Drpc__Response *resp = drpc_response_create(call);
    session_ctx->handler(call, resp);
    ```
    您的会话处理程序应在响应中填写任何错误或有效负载。
7. 将响应发送给调用者并清理：
    ```
    int result = drpc_send_response(session_ctx, resp);
    if (result != 0) {
        /* process errors */
    }
    drpc_response_free(resp);
    drpc_call_free(call);
    ```
8. 如果客户端已关闭连接，请关闭会话上下文以释放指针：
    ```
    drpc_close(session_ctx);
    ```
9. 如上所述，当需要关闭服务器时，请关闭所有打开的会话上下文。然后`drpc_close()`**监听上下文**。

## Checksum

### Checksummer

A "Checksummer" is used to create checksums from a scatter gather list. The
 checksummer uses a function table (specified when initialized) to adapt common
 checksum calls to the underlying library that implements the checksum
 algorithm.
 Currently the isa-l and isa-l_crypto libraries are used to support crc16,
 crc32, crc64, and sha1. All of the function tables to support these
 algorithms are in [src/common/checksum.c](checksum.c).
 These function tables
 are not made public, but there is a helper function (daos_mhash_type2algo) that
 will return the appropriate function table given a DAOS_CSUM_TYPE. There is
 another helper function (daos_contprop2csumtype) that will convert a container
 property value to the appropriate DAOS_CSUM_TYPE. The double "lookups" from
 container property checksum value to function table was done to remove the
 coupling from the checksummer and container info.

 All checksummer functions should start with daos_csummer_* and take a struct
 daos_csummer as the first argument. To initialize a new daos_csummer,
 daos_csummer_init takes the address to a pointer (so memory can be allocated),
 the address to a function table implementing the desired checksum algorithm,
 and because this is a DAOS checksummer, the size to be used for "chunks" (See
 [VOS](/src/vos/README.md) for details on chunks and chunk size). If it wasn't
 for the need to break the incoming data into chunks, the checksummer would not
 need the chunk size. When done with a checksummer, daos_csummer_destroy should
 be called to free allocated resources.
 Most checksummer functions are simple passthroughs to the function table if
 implemented. The main exception is daos_csummer_calc which, using the
 other checksummer functions, creates a checksum from the appropriate memory
 represented by the scatter gather list (d_sg_list_t) and the extents
 (daos_recx_t) of an I/O descriptor (daos_iod_t).
 The checksums are put into a collection of checksum buffers
 (daos_csum_buf_t), each containing multiple checksums. The memory for the
 daos_csum_buf_t's and the checksums will be allocated. Therefore, when done
 with the checksums, daos_csummer_destroy_csum_buf should be called to free
 this memory.

 There are a set of helper functions (prefixed with dcb) to act on a
 daos_csum_buf_t. These functions should be straight forward. The
 daos_csum_buf_t contains a pointer to the first byte of the checksums
 and information about the checksums, including count, size of
 checksum, etc.
