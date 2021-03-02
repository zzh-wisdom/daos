# gRPC Go快速开始

参考：

- Quick Start：<https://www.grpc.io/docs/languages/go/quickstart/>
- 基础教程：<https://www.grpc.io/docs/languages/go/basics/>

## 依赖安装 Ubuntu

### Go编译器

```shell
sudo add-apt-repository ppa:longsleep/golang-backports
sudo apt update
sudo apt install golang-go
```

### protoc编译器

```shell
$ apt install -y protobuf-compiler
$ protoc --version  # Ensure compiler version is 3+
```

### go插件

需要开启代理，或者使用国内镜像。

```shell
$ export GO111MODULE=on  # Enable module mode
$ go get google.golang.org/protobuf/cmd/protoc-gen-go \
         google.golang.org/grpc/cmd/protoc-gen-go-grpc
```

设置环境

```shell
export PATH="$PATH:$(go env GOPATH)/bin"
```

接着Quick Start的教程来就行。

sudo chmod 777 grpc-go -R # 将文件夹权限改成所有用户具有所有的权限
