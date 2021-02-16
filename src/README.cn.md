# DAOS Internals

- [1. DAOS Components](#1-daos-components)
  - [1.1. DAOS System](#11-daos-system)
  - [1.2. Client APIs, Tools and I/O Middleware](#12-client-apis-tools-and-io-middleware)
  - [1.3. Agent](#13-agent)
- [2. 网络运输和通信](#2-网络运输和通信)
  - [2.1. gRPC and Protocol Buffers](#21-grpc-and-protocol-buffers)
  - [2.2. dRPC](#22-drpc)
  - [2.3. CART](#23-cart)
- [3. DAOS 分层和服务](#3-daos-分层和服务)
  - [3.1. Architecture](#31-architecture)
  - [3.2. 源代码结构](#32-源代码结构)
  - [3.3. 基础设施库](#33-基础设施库)
  - [3.4. DAOS服务](#34-daos服务)
- [4. 软件兼容性](#4-软件兼容性)
  - [4.1. 协议兼容性](#41-协议兼容性)
  - [4.2. PM Schema（模式/架构）兼容性和升级](#42-pm-schema模式架构兼容性和升级)

原文：<https://github.com/daos-stack/daos/blob/master/src/README.md#42>

本文档的目的是描述DAOS使用的内部**代码结构**和主要**算法**。这里假定你已经了解了[DAOS存储模型](https://github.com/daos-stack/daos/blob/master/doc/overview/storage.md)和[术语表](https://github.com/daos-stack/daos/blob/master/doc/overview/terminology.md)。

## 1. DAOS Components

如下图所示，DAOS安装涉及多个可以**共置或分布**的组件。 DAOS软件定义的存储（SDS）框架依赖于两个不同的通信通道：用于**管理**的带外（out-of-band）TCP/IP网络和用于**数据访问**的高性能fabric。实际上，管理和数据访问可以使用相同的网络。fabric(光纤)网络上的IP也可以用作网络管理。

![DAOS SDS Components](/doc/graph/system_architecture.png)

### 1.1. DAOS System

**DAOS服务器**在Linux实例（即物理节点，VM或容器）上运行，是**管理**分配给DAOS的本地连接的SCM和NVM存储的多租户守护程序。它侦听由**IP地址和TCP端口号**寻址的管理端口，以及由**网络URL**寻址的一个或多个fabric端点。DAOS服务通过YAML文件（`/etc/daos/daos_server.yml`，或命令行上提供的其他路径）配置器。可以将DAOS服务器的启动和停止与不同的守护程序管理或业务流程框架集成（例如systemd脚本，Kubernetes服务或甚至通过并行启动器（如pdsh或srun））。

DAOS系统由**系统名称**标识，并且由连接到同一fabric的一组DAOS服务器组成。两个不同的系统包括两组不相交的服务器，并且彼此不协作。 DAOS池不能跨越多个系统。

在内部，DAOS服务器由多个守护进程组成。第一个要启动的是[控制平面](https://github.com/daos-stack/daos/blob/master/src/control/README.md)（名为daos_server的二进制文件），它负责解析配置文件，配置存储并最终启动和监视**一个或多个**[数据平面](https://github.com/daos-stack/daos/blob/master/src/engine/README.md)实例（二进制文件daos_engine）。

**控制平面**用Go编写，并在gRPC框架上实现了DAOS管理API，该API提供了安全的带外通道来管理DAOS系统。可以通过`daos_server.yml` YAML配置文件配置每个服务器要启动的数据平面实例的数量以及存储、CPU和fabric接口的关联性。

**数据平面**是用C编写的运行DAOS存储引擎的多线程进程。它通过CART通信中间件处理传入的元数据和I/O请求，并通过PMDK（对于存储类内存，又名SCM）和SPDK（对于NVMe SSD）库访问本地NVM存储（这里的NVM指广义的持久性存储）。数据平面依靠Argobots来进行基于事件的并行处理，并导出可以通过fabric独立寻址的多个目标target。在DAOS系统中，每个数据平面实例均被分配一个唯一的等级。

控制平面和数据平面进程通过**Unix域套接字**和称为**dRPC**的自定义轻量级协议进行**本地通信**。

延伸阅读：

- [DAOS 控制平面 (daos_server)](./1.1.1.控制平面.md)
- [DAOS 数据平面 (daos_engine)](./1.1.2.数据平面.md)

### 1.2. Client APIs, Tools and I/O Middleware

应用程序、用户和管理员可以通过**两个不同的客户端 API** 与 DAOS 系统进行交互。

1. DAOS 管理 Go 包允许可以通过**带外管理通道**与 DAOS 服务器通信的任何节点管理 DAOS 系统。此 API 保留给**通过特定证书进行身份验证**的 DAOS 系统管理员。2. DAOS 管理 API 旨在与不同的特定于供应商的存储管理或开源业务流程框架集成。在 DAOS 管理 API 上构建了名为 `dmg` 的 CLI 工具。用于进一步阅读管理 API 和 `dmg` 工具：

- [DAOS management Go package](https://godoc.org/github.com/daos-stack/daos/src/control/client)
- [DAOS Management tool (aka dmg)](https://github.com/daos-stack/daos/blob/master/src/control/cmd/dmg/README.md)

DAOS 库 （`libdaos`） 实现 DAOS 存储模型，主要面向希望将数据集存储到 DAOS 容器中的应用程序和 I/O 中间件开发人员。它可以从连接到fabric(目标 DAOS 系统使用的网络)的任何节点使用。应用程序进程通过 DAOS 代理进行身份验证（请参阅下一节）。`libdaos` 导出的 API 通常称为 DAOS API（与 DAOS 管理 API 不同），允许通过不同的接口（例如键值存储或数组 API）管理容器和访问 DAOS 对象。`libdfs` 库通过 `libdaos` 模拟 POSIX 文件和目录的抽象，并为需要 POSIX 命名空间的应用程序提供平滑的迁移路径。有关 `libdaos`、不同编程语言的绑定和 `libdfs` 的进一步阅读：

- <a href="client/api/README.md">DAOS Library (`libdaos`)</a> and <a href="client/array/README.md">array interface</a> and <a href="client/kv/README.md">KV interface</a> built on top of the native DAOS API</a>
- <a href="src/client/pydaos/raw/README.md">Python API bindings</a>
- <a href="https://github.com/daos-stack/go-daos">Go bindings</a> and <a href="https://godoc.org/github.com/daos-stack/go-daos/pkg/daos">API documentation</a>
- <a href="client/dfs/README.md">POSIX File & Directory Emulation (`libdfs`)</a>

`libdaos` 和 `libdfs` 库提供了支持特定领域的数据格式（如 HDF5 和 Apache Arrow）的基础。有关 I/O 中间件集成的进一步阅读，请查看以下外部参考文献： 

- <a href="https://bitbucket.hdfgroup.org/projects/HDFFV/repos/hdf5/browse?at=refs%2Fheads%2Fhdf5_daosm">DAOS VOL connector for HDF5</a>
- <a href="https://github.com/daos-stack/mpich/tree/daos_adio">ROMIO DAOS ADIO driver for MPI-IO</a>

### 1.3. Agent

[DAOS 代理](/src/control/cmd/daos_agent/README.md)是驻留在客户端节点上的守护进程。**它通过 dRPC 与 DAOS 客户端库（library）进行交互，以*验证*应用程序进程**。它是一个受信任的实体，可以使用本地证书对 DAOS 客户端凭据进行签名。DAOS 代理可以支持不同的身份验证框架，并使用 Unix 域套接字与客户端库通信。**DAOS 代理以 Go 编写，并通过 gRPC 与每个 DAOS 服务器的控制平面组件进行通信，以便向客户端库提供 DAOS 系统成员身份信息和支持池列表操作（listing）**。

## 2. 网络运输和通信

如上一节所述，DAOS 使用三种不同的通信通道。

### 2.1. gRPC and Protocol Buffers

gRPC 为 DAOS 管理提供了双向安全通道。它依靠 TLS/SSL 对管理员角色和服务器进行身份验证。Protocol buffers 用于 RPC 序列化，所有proto文件都位于[proto](/src/proto)目录中。

### 2.2. dRPC

dRPC 是通过 Unix 域套接字构建的通信通道，用于进程间通信。它同时提供 C 和 Go 接口，以支持：

- daos_agent 和 libdaos 用于应用程序验证
- daos_server（控制平面）和daos_engine（数据平面）守护进程之间类似于 gRPC，RPC 通过protocol buffers进行序列化。

### 2.3. CART

[CART](https://github.com/daos-stack/cart) 是一个用户空间函数扩展库（也是一种RPC），为 **DAOS 数据平面**提供低延迟高带宽通信。**它支持 RDMA 功能和可扩展的集体操作**。CART 建在 [Mercury](https://github.com/mercury-hpc/mercury) 和 [libfabric](https://ofiwg.github.io/libfabric/) 之上。CART 库用于 `libdaos` 和 `daos_engine` 实例之间的所有通信。

## 3. DAOS 分层和服务

### 3.1. Architecture

如下图所示，DAOS 堆栈是通过客户端/服务器体系结构构建的存储服务的集合。DAOS 服务的示例包括池、容器、对象和重建服务。

![DAOS Internal Services & Libraries](/doc/graph/services.png)

DAOS 服务可以跨控制平面和数据平面进行传播，并通过 dRPC 进行内部通信。**大多数服务都有可通过 gRPC 或 CART 同步的客户端和服务器组件**。跨服务通信始终通过直接 API 调用完成。这些函数调用可以在服务的客户端或服务器组件之间调用。虽然每个 DAOS 服务都设计为相当自主和隔离，但有些服务比另一些服务更紧密耦合。这通常是重建服务的情况，需要与池、容器和对象服务密切交互，以在 DAOS 服务器发生故障后恢复数据冗余。

基于服务的体系结构提供了灵活性和可扩展性，但它与一组基础结构库相结合，这些基础结构库提供丰富的软件生态系统（例如通信、持久存储访问、具有依赖关系图的异步任务执行、加速器支持等），可供所有 DAOS 服务访问。

### 3.2. 源代码结构 

每个基础结构库和服务在 `src/` 下分配一个专用目录。服务的客户端和服务器组件存储在单独的文件中。那些属于客户端组件一部分的函数名带有 `dc_`（代表 DAOS Client）前缀，而服务器端函数带有 `ds_`（代表 DAOS Server）前缀。客户端和服务器组件之间使用的协议和 RPC 格式通常在名为 `rpc.h` 的头文件中定义。

在控制平面的上下文中执行的所有 Go 代码都位于 `src/control` 目录下。管理和安全性是分布在控制（Go 语言）和数据 （C 语言） 平面上的服务，并通过 dRPC 在内部进行通信。

向最终用户（如 I/O 中间件或应用程序开发人员）公开的官方 DAOS API 的头文件在 `src/include` 下，并使用 `daos_` 前缀。每个基础结构库导出一个 API，该 API 在 `src/include/daos` 下可用，**可由任何服务使用**。给定服务导出的客户端 API（具有 `dc_` 前缀）也存储在 `src/include/daos` 下，而服务器端接口（带 `ds_` 前缀）存储在 `src/include/daos_srv` 下。

### 3.3. 基础设施库

GURT 和通用 DAOS（即 `libdaos_common`）库向 DAOS 服务提供日志记录、调试和通用数据结构（例如哈希表、btree 等）。

本地 NVM 存储由版本控制对象存储 （Versioning Object Store，VOS） 和 Blob I/O （BIO） 库管理。VOS 在 SCM 中实现**持久索引**，而 BIO 负责根据分配策略将应用程序**数据**存储在 NVMe SSD 或 SCM 中。VEA 层集成到 VOS 中，并管理 NVMe SSDs 的块分配。

DAOS 对象分布在多个目标中，用于性能（如分片）和恢复能力（如复制或擦除代码）。**放置库**实现不同的算法（例如基于环的放置、跳转一致的哈希等），以从目标和对象标识符列表中生成对象的布局。

复制服务 （RSVC） 库最终提供了一些通用代码来支持容错。池、容器和管理服务与 RDB 库一起使用，RDB 库在 Raft 上实现冗余的键值存储。

有关这些基础结构库的进一步阅读，请参阅： 

- <a href="common/README.md">Common Library</a>
- <a href="vos/README.md">Versioning Object Store (VOS)</a>
- <a href="bio/README.md">Blob I/O (BIO)</a>
- <a href="placement/README.md">Algorithmic object placement</a>
- <a href="rdb/README.md">Replicated database (RDB)</a>
- <a href="rsvc/README.md">Replicated service framework (RSVC)</a>

### 3.4. DAOS服务

下图显示了 DAOS 服务的内部分层以及与上述不同库的交互。

![DAOS Internal Layering](/doc/graph/layering.png)

垂直框表示 DAOS 服务，而水平框表示基础结构库。

有关每个服务的内部的进一步阅读： 

- <a href="pool/README.md">Pool service</a>
- <a href="container/README.md">Container service</a>
- <a href="object/README.md">Key-array object service</a>
- <a href="rebuild/README.md">Self-healing (aka rebuild)</a>
- <a href="security/README.md">Security</a>

## 4. 软件兼容性

DAOS 中的互操作性通过持久数据结构的协议和模式版本（protocol and schema versioning）处理。

### 4.1. 协议兼容性

DAOS 存储堆栈提供有限的协议互操作性。版本兼容性检查将被执行，以验证：

- 同一池中的所有目标都运行相同的协议版本
- 与应用程序链接的客户端库可能最多有一个协议版本比目标要旧。

如果在同一池中的存储目标之间检测到协议版本不匹配，则整个 DAOS 系统将无法启动，并将故障报告给控制 API。同样，运行与目标不兼容的协议版本的客户端的连接将返回错误。

### 4.2. PM Schema（模式/架构）兼容性和升级 

持久数据结构的架构可能会不时演变，以修复错误、添加新优化或支持新功能。为此，持久数据结构支持架构版本控制。

升级架构版本不是自动完成的，必须由管理员启动。将提供专用的升级工具，将架构版本升级到最新的版本。**同一池中的所有目标都必须具有相同的架构版本。版本检查在系统初始化时执行，以强制执行此约束。**

为了限制验证矩阵（matrix，系统环境的意思），每个新的 DAOS 版本都将发布一些受支持的架构Schema版本列表。若要使用新的 DAOS 版本运行，管理员将需要将 DAOS 系统升级到受支持的架构版本之一。新目标将始终使用最新版本重新格式化。**此版本控制架构仅适用于存储在持久内存中的数据结构，而不适用于仅存储没有元数据的用户数据的块存储**。
