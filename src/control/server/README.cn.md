# DAOS Server

- 控制服务器：control server

server包实现 DAOS 控制服务器的内部细节，daos_server是一个面向用户的应用程序，由[`daos_server`](/src/control/cmd/daos_server/README.md)包实现。

## 1. I/O 引擎实例

DAOS I/O 服务器进程（daos_engine 二进制文件）由 DAOS 控制服务器（daos_server 二进制文件）fork 出来，并执行 DAOS 的主要用户空间 I/O 操作。[instance.go](/src/control/server/instance.go) 提供了 `EngineInstance` 抽象和相关方法。

用于 I/O Server 进程控制和配置的基础抽象封装在[engine](/src/control/server/engine) 包中。

## 2. I/O 服务器套件(Harness)

DAOS I/O 服务器进程由 DAOS 控制服务器管理和监视，并且逻辑上驻留在 I/O 服务器套件的成员中。[harness.go](/src/control/server/harness.go) 提供了 `EngineHarness` 抽象和相关方法。

## 3. 通信

DAOS 服务器使用 gRPC protocol 来与客户端 gRPC 应用程序通信，并通过 Unix 域套接字与 DAOS I/O 引擎进行交互。

控制服务器加载多个 gRPC 服务器模块，目前包含的模块是安全(security)和管理(management)。

控制平面服务器 （daos_server） 实例将打开 gRPC 通道以**侦听来自控制平面客户端应用程序的请求**。

[server.go](/src/control/server/server.go) 包含主要的设置例程，包括建立 gRPC 服务器和注册 RPC。

### 3.1. Control Service 控制服务

gRPC 服务器注册控制服务(在protoc文件中进行定义)以处理来自管理工具的请求。

控制服务请求是在一个或多个节点上并行执行的操作（例如硬件预配），它将通过存储或网络库执行。

此类广播命令（在连接到主机列表后将发出）通常由[管理工具](/src/control/cmd/dmg/README.md)，即 gRPC 客户端发出。

这些命令不会触发 dRPC，但将执行节点本地功能，如硬件（网络和存储）预配。

控制服务 RPC 处理程序代码包含在 /src/control/server/ctl_*.go 文件。protobuf特定的 解包/打包代码在 /src/control/server/ctl_*_rpc.go 文件中。

### 3.2. Management Service 管理服务

作为 DAOS 服务器的一部分，Control 平面实现了一个管理服务，**负责处理整个 DAOS 系统的分布式操作**。

MS 命令（连接到接入点主机时将发出）通常由管理工具 gRPC 客户端发出。

MS 命令将从管理工具触发，并在作为接入点运行的存储节点上处理。请求将通过 dRPC 通道转发到数据平面（[引擎](/src/engine)），并由 [mgmt](/src/mgmt/srv.c) 模块处理。

管理服务 RPC 处理程序代码包含在 [mgmt_svc.go](/src/control/server/mgmt_svc.go) 中。

### 3.3. Fanout 扇出

某些控制服务 RPC 处理程序将通过 gRPC 触发多个远程线束（套件）的扇出，以便发送使用 rpcClient 的这些扇出请求。

在多个远程线束上执行 gRPC 扇出的管理工具命令的示例是 `dmg system query`，它的服务器端处理程序是 [ctl_system.go](/src/control/server/ctl_system.go) 中为 `SystemQuery`，它使用 [system.go](/src/control/lib/control/system.go)(实现一元调用接口) 中的 rpcClient 函数向远程线束发出请求。

### 3.4. 系统命令

系统命令使用扇出，并发送一元 RPC 到系统中选定的排名，用于操作查询、停止、启动和重新格式化。

这些操作从客户端应用程序（如 dmg）启动，并使用控制 API。有关详细信息，请参阅 [ctl_system.go](/src/control/server/ctl_system.go)、[system.go](/src/control/lib/control/system.go) 和 [mgmt_system.go](/src/control/server/mgmt_system.go) 中的代码文档。

### 3.5. 存储

与存储相关的 RPC，其处理程序在 [ctl_storage*.go](/src/control/server/ctl_storage.go) 中将操作委派到封装在 `bdev` 和 `scm` [存储子包](/src/control/server/storage)中的后端提供程序。

## 4. 引导和DAOS系统成员资格关系

在启动数据平面实例时，我们**查看超级块以确定它是否应是 MS（管理服务）副本**。daos_server.yml 的 access_points 参数（仅在格式期间）用于确定实例是否为 MS 副本。

当启动实例标识为 MS 副本时，它将执行引导并启动。如果 DAOS 系统只有一个副本（由 access_points 参数指定），则引导实例的主机现在是 MS 领导。而如果有多个副本，选举将在后台进行，并最终选出一位领导人。

当起始实例未标识为 MS 副本时，实例的主机通过 gRPC 调用 MgmtSvcClient 上的Join联接，包含向 MS 领导发送的请求中的实例的主机 ControlAddress（gRPC 服务器正在侦听的地址，这个所谓的gRPC服务器在MS领导者中）。

在 MS 领导上运行的 MgmtSvc 处理 gRPC 服务器收到的Join联接请求，并通过 dRPC 将请求转发到 MS 领导实例。如果联接请求成功，则 MS 领导者的 MgmtSvc 将请求中包含的地址记录为**新的系统成员**。

## 5. 存储管理

在 NVMe SSD 设备上的操作使用 [go-spdk bindings](/src/control/lib/spdk) 执行，通过 SPDK 框架原生 C 库发出命令。

SCM 持久内存模块上的操作使用 [go-ipmctl bindings](/src/control/lib/ipmctl) 执行，通过 ipmctl 原生 C 库发出命令。

### 5.1. 存储格式

在开始 DAOS 数据平面之前，需要设置存储的格式。

![Storage format diagram](/doc/graph/storage_format_detail.png)

如果存储之前还未格式化，daos_server启动后将停止，等待通过管理工具触发存储格式。

#### SCM Format

格式化 SCM 涉及在 nvdimm 设备上创建 ext4 文件系统。挂载（Mounting） SCM 会导致使用 DAX 扩展实现活动装载，从而实现直接访问，而不受传统块存储的限制。

SCM 设备命名空间的格式化和挂载按配置文件中的参数（带有 `scm_` 前缀）设置来执行。

#### NVMe Format

在控制平面准备 NVMe 设备以使用 DAOS 数据平面操作的情况下，"formatting"是指存储介质的重置，该存储介质将删除 blobstores 并从 SSD 控制器命名空间中删除任何文件系统签名。

当配置文件中的 `bdev_class` 参数等于 `nvme` 时，将在 PCI 地址（由配置文件的参数 `bdev_list` 指定）标识的设备上执行格式化操作。

为了指定 DAOS 数据平面实例使用的 NVMe 设备，控制平面将生成一个 `daos_nvme.conf` 文件，由 SPDK 使用该文件，该文件将写入 scm_mount（持久）安装位置，作为在写入超级块之前格式化的最后阶段，表示服务器已格式化。

## Architecture

DAOS 软件组件体系结构视图： 

![Architecture diagram](/doc/graph/system_architecture.png)

## Running

有关构建和运行 DAOS 的说明，请参阅[admin guide](https://daos-stack.github.io/admin/installation/).

## Configuration

有关配置 DAOS 服务器的说明，请参阅[admin guide](https://daos-stack.github.io/admin/deployment/#server-configuration-file).




























