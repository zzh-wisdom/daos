# DAOS Management Tool (dmg)

DAOS 管理工具是 DAOS 的系统管理应用程序。**与其他需要通过 DAOS 代理进行身份验证的 DAOS 客户端应用程序不同，dmg 能够直接通过本地证书进行身份验证。**

管理工具具有有限的软件依赖项，因为任务通过 RPC 在远程服务器上执行，因此适合在登录节点上运行。

## 命令行接口

[go-flags](https://github.com/jessevdk/go-flags)包用于实现命令行接口。命令树的构建基于在 main.go 中定义的 `cliOptions` 结构中定义的顶级命令，然后子命令在功能特定的文件中定义，如下所述。然后在子命令类型的 `Execute` 方法中定义子命令执行的所需行为。

## 证书 Certificates

可以在[客户端配置文件](/utils/config/daos_control.yml)中指定本地证书位置。

有关在 DAOS 中如何使用证书的更多详细信息，请参阅[安全文档](/src/control/security/README.md#certificate-usage-in-daos)。

## Communications

管理工具是 gRPC 客户端，使用 [control API](/src/control/lib/control) 与多个[daos_server](/src/control/cmd/daos_server/README.md)并行交互。control API 调用发给 daos_server 实例的RPC，每个实例运行托管control服务的侦听 gRPC 服务器。

通信通过**管理网络**进行，要连接到的远程存储服务器的地址可以指定为 LLNL 风格的主机列表（在命令行或[客户端配置文件](/utils/config/daos_control.yml)中指定为许多类似名称的主机的紧凑表示形式，例如 `foo[1-1024]`、`bar[35，42，55-64]`等）。

有关如何保护 gRPC 通信进行身份验证的详细信息，请参阅[安全文档](/src/control/security/README.md#host-authentication-with-certificates)。

## 使用文档

当增加或者修改命令时，`dmg` 的 man 页面可以通过运行 `go test -v -run TestDmg_ManPageIsCurrent -args --update` 来自动更新，如果未运行，单元测试将失败。具体实现在 `man_test.go` 中。

## Functionality 功能

管理工具提供的功能被拆分为映射到各个子命令的域。

有关详尽的子命令列表，请参阅 `dmg [<subcommand>]  --help` 的输出。

### Config

提供自动生成给定主机集的建议服务器配置文件的功能。在 `auto.go` 中实现。

### Container

提供更改 DAOS 容器所有者的功能。在 `cont.go` 中实现。

### Firmware 固件

固件的相关功能是选择性编译的，可能并非存在于所有生成的dmg中。提供查询和更新 NVMe 和 PMem 设备上固件的功能。在 `firmware.go` 中启用固件的实现。禁用时则使用`firmware_disabled.go`。

### Network

提供管理和枚举可用网络设备的功能。在 `network.go` 中实现.

### Pool

提供创建、销毁和管理 DAOS 存储池的功能，包括管理单个属性和池访问控制列表（Access Control Lists）的功能。Online Server Addition 相关命令提供消耗、驱逐、排除、扩展和重新集成的drain。具体实现在 `pool.go` 中，ACL 功能和帮助器在 `acl.go` 中。

### Storage

提供扫描可用存储设备、预配相关硬件和格式分配的存储以用于 DAOS 的功能。在 `storage.go` 中实现。

还提供在特定设备上运行查询以及设置特定设备属性的功能。dmg 的子命令 `storage query ...` 在 `storage_query.go` 中实现。

### System

提供查询以前加入 DAOS 系统的系统成员/等级的功能。此外，对系统成员身份中记录的成员/等级（的系统）执行受控的停止和启动。在 `system.go` 中实现。

## 单元测试

每个功能文件都提供单元测试（文件名后缀为 `_test.go`）。命令语法通过测试进行验证，这些测试调用到`command_test`中的帮助器方法里。命令处理程序会被自动检查，以验证在设置 --json 标志时提供有意义的输出。
