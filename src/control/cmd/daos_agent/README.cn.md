# DAOS 代理

DAOS 代理是一个后台进程，充当客户端应用程序和 DAOS 系统之间的可信中介。**DAOS 代理进程必须在每个计算节点上运行，才能成功执行这些节点上的客户端操作**。

## 1. Certificate 证书

如果系统设置在正常模式（不是不安全模式），则代理将配置具有通用名称"agent"的证书，以标识自身并证明用户凭据的有效性。

有关在 DAOS 中如何使用证书的更多详细信息，请参阅[安全文档](/src/control/security/README.md#certificate-usage-in-daos)。

## 2. 客户端通信

客户端进程使用在计算节点上设置的 UNIX 域套接字与 DAOS 代理通信。（客户端运行在计算节点上？）

### 2.1. UNIX 域套接字

用于 UNIX 域套接字的默认目录是 `/var/run/daos_agent`。或者，也可以在代理配置文件中指定目录。**目录必须存在，启动代理进程的用户必须具有对该进程的写入访问权限，代理才能成功启动**。代理打开套接字，继续侦听来自**本地客户端**的通信。

DAOS 客户端库不读取配置文件。如果对套接字使用非默认位置，则必须在环境变量 `DAOS_AGENT_DRPC_DIR` 中指定此目录，以便客户端库与代理通信。

### 2.2. dRPC

用于在客户端和代理之间通信的协议是 [dRPC](/src/control/drpc/README.md)。所需的 dRPC 通信被整合到客户端库中，客户端 API 用户不可见。

## 3. 服务器通信

代理和 DAOS 控制平面服务器之间的通信通过管理网络的 gRPC 协议发生。接入点服务器在代理的配置文件中定义。

有关如何保护 gRPC 通信进行身份验证的详细信息，请参阅[安全文档](/src/control/security/README.md#host-authentication-with-certificates)。

## 4. 功能 Functionality

代理提供的功能包括： 

- 获取 DAOS 服务器排名的附加信息。
- 生成一个签名客户端凭据，该凭据由服务器用于访问控制决策。

这些功能由客户端通过 [dRPC protocol](/src/control/cmd/daos_agent/README.md#client-communications) 访问。

### 4.1. 获取附加信息

客户端通信通过高速fabric发送到数据平面引擎。最初，客户端不知道这些服务器等级的 URIs。主服务排名（The Primary Service Ranks，PSR） 是客户端可以查询的引擎，用于获取群集中特定等级的 URI。客户端库知道 PSR 并选择了适当的网络设备后，将初始化通过 CaRT 的客户端通信，并自动查询 PSR 以将 RPC **引导到正确的排名**。

若要获取 PSR 和网络配置，客户端进程必须向代理发送获取附加信息请求。代理进程首次收到请求时，它将初始化 Get Attach Info 响应的缓存。为了填充缓存，代理会通过管理网络将此请求转发到接入点的控制平面服务器。控制平面服务器再次将请求转发到本地数据平面实例，该实例将检查 PSR 并返回信息给代理。然后，代理执行本地fabric扫描，以确定哪些可用的网络设备支持由daos_server使用的fabric 提供程序。它确定找到的每个匹配网络设备的 NUMA 相关性。代理将完全编码的响应存储到缓存中，缓存封装每个 NUMA 节点的 PSR 和网络配置。支持每个 NUMA 节点的多个网络设备。每个设备和 NUMA 节点的组合在缓存中都有自己的条目。

此时，"Get Attach Info"缓存已初始化。对于此请求和所有后续请求，代理将检查与请求关联的客户端 PID 以确定其 NUMA 绑定（如果可用）。然后，代理索引到缓存中，以检索与客户端的 NUMA 相关性匹配的网络设备缓存响应，然后将该响应返回给客户端，而无需在堆栈上进一步通信。如果无法确定客户端 NUMA 相关性，或者如果没有共享相同的相关性的可用网络设备，则从已知网络设备选择默认响应。如果没有网络设备，则选择使用环回（loopback）设备编码的响应。

如果有多个共享相同 NUMA 相关性的可用网络设备，则缓存将包含每个设备的条目。代理使用循环（round-robin）选择算法选择同一 NUMA 节点中的响应。

获取附加信息（Get Attach Info）的负载包含网络配置参数，其中包括OFI_INTERFACE、OFI_DOMAIN CRT_TIMEOUT、provider和CRT_CTX_SHARE_ADDR。OFI_INTERFACE、OFI_DOMAIN和CRT_TIMEOUT变量可以在启动之前在客户端环境中设置任何这些环境变量来覆盖（旧值）。daos 客户端库将使用获取附加信息请求提供的值（如果是重写，则不使用）初始化 CaRT。

**客户端需要此信息才能发送任何 RPC.**

### 4.2. 请求客户端凭据

某些客户端操作（如连接到池）由访问控制监管。由于客户端库可能被篡改或替换，因此不能信任它执行自己的访问检查或生成自己的凭据。相反，客户端必须查询代理以验证有效用户的标识。服务器使用此标识信息进行访问控制检查。

若要请求其用户标识的凭据，客户端向代理发送请求凭据请求。代理检查 UNIX 域套接字（用于发送请求以获取有效用户的 UID），从中，它生成一个身份验证令牌Auth Token。身份验证令牌是凭据负载和验证器值，可用于确认凭据的完整性。身份验证令牌将返回到客户端库，该库将它打包到二进制 Blob 中。这可以作为 RPC 负载的一部分发送到数据平面服务器。

在安全模式下，验证器是打包的凭据上的签名，使用代理证书的私钥。当数据平面服务器收到此凭据时，它可以使用每台服务器拥有的一组已知代理证书来验证签名。这保证了凭据由代理生成，不会欺骗。

在不安全的模式下，验证器只是凭据数据的哈希。这可以验证凭据在传输过程中未损坏，但无法提供其他方式下防止篡改的保护。


## 其他

### 环境变量

- `DAOS_AGENT_DISABLE_CACHE`:是否开启GetAttachInfo缓存