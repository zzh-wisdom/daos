# Protobuf定义

proto目录包含用于通过gRPC和dRPC通道进行通信的消息传递格式的[protocol buffer](https://developers.google.com/protocol-buffers)定义。
有关使用protobuf工具进行开发的信息，请参见[此处](https://github.com/zzh-wisdom/daos/blob/learning/doc/dev/development.md#protobuf-compiler)。

**修改这些文件时，请确保更新所有生成的文件**。可以使用以下列出的命令来生成相关文件，这些命令是从DAOS源代码的src/proto顶级目录中发出（运行）的：

- 为控制平面生成的文件采用Golang语言，生成的文件在 `src/control` 中，并且文件扩展名为`.pb.go`。命令语法示例：

    ```shell
    protoc -I mgmt --go_out=plugins=grpc:control/common/proto/mgmt mgmt/storage.proto
    ```

- 为数据平面生成的文件采用C语言，生成的文件在其他`src`子目录中，并且文件扩展名为.pb-c.[ch]。需要第三方插件[protobuf-c](https://github.com/protobuf-c/protobuf-c)才能生成C语言的pb文件。命令语法示例:

    ```shell
    protoc -I mgmt --c_out=../mgmt mgmt/srv.proto --plugin=/opt/potobuf/install/bin/protoc-gen-c
    ```

