# Go 指导

本教程使用协议缓冲区语言的`proto3`版本，向Go程序员介绍了如何使用协议缓冲区。通过创建一个简单的示例应用程序，它向您展示了如何

- 在`.proto`文件中定义消息格式。
- 使用协议缓冲区编译器。
- 使用Go协议缓冲区API写入和读取消息。

这不是在Go中使用协议缓冲区的全面指南。有关更多详细的参考信息，请参阅[协议缓冲区语言指南](https://developers.google.com/protocol-buffers/docs/proto3)，[Go API参考](https://pkg.go.dev/google.golang.org/protobuf/proto)，[Go生成的代码指南](https://developers.google.com/protocol-buffers/docs/reference/go-generated)和[编码参考](https://developers.google.com/protocol-buffers/docs/encoding)。

## 为什么要使用协议缓冲区？

我们将使用的示例是一个非常简单的“地址簿”应用程序，它可以在文件中读取和写入人们的联系方式。通讯录中的每个人都有一个姓名name，一个ID，一个电子邮件地址和一个联系电话。

您如何像这样序列化和检索结构化数据？有几种方法可以解决此问题：

- 使用[gobs](https://golang.org/pkg/encoding/gob/)序列化Go数据结构。在Go特定的环境中，这是一个很好的解决方案，但是如果您需要与为其他平台编写的应用程序共享数据，它就不能很好地工作。
- 您可以发明一种将数据项编码为单个字符串的特殊方法，例如将4个整数编码为“12:3:-23:67”。尽管确实需要编写一次性编码和解析代码，但是这是一种简单且灵活的方法，并且解析会带来少量的运行时成本。这对编码非常简单的数据最有效。
- 将数据序列化为XML。由于XML是人类（一种）可读的，并且存在用于多种语言的绑定库，因此该方法可能非常有吸引力。如果要与其他应用程序/项目共享数据，这可能是一个不错的选择。但是，众所周知，XML占用大量空间，对它进行编码/解码会给应用程序带来巨大的性能损失。同样，导航XML DOM树比通常导航类中的简单字段要复杂得多。

协议缓冲区是灵活、高效、自动化的解决方案，可以准确地解决此问题。使用协议缓冲区，您可以编写要存储的数据结构的`.proto`描述。由此，协议缓冲区编译器创建了一个类，该类以有效的二进制格式实现协议缓冲区数据的自动编码和解析。生成的类为构成协议缓冲区的字段提供了获取器和设置器，并以协议为单位来处理读取和写入协议缓冲区的详细信息。重要的是，协议缓冲区格式支持以某种方式扩展格式的思想，以使代码仍可以**读取以旧格式编码的数据**。

## 在哪里找到示例代码

我们的示例是一组命令行应用程序，用于管理使用协议缓冲区编码的地址簿数据文件。命令`add_person_go`将新条目添加到数据文件。命令`list_people_go`解析数据文件并将数据打印到控制台。

您可以在GitHub存储库的[examples目录](https://github.com/protocolbuffers/protobuf/tree/master/examples)中找到完整的示例。

## 定义协议格式

要创建地址簿应用程序，您需要以`.proto`文件开头。 .proto文件中的定义很简单：您为要序列化的每个数据结构添加一条消息，然后为消息中的每个字段指定名称和类型。在我们的示例中，定义消息的.proto文件是[addressbook.proto](https://github.com/protocolbuffers/protobuf/blob/master/examples/addressbook.proto)。

.proto文件以程序包声明开头，这有助于防止不同项目之间的命名冲突。、

```go
syntax = "proto3";
package tutorial;

import "google/protobuf/timestamp.proto";
```

`go_package`选项定义了软件包的导入路径，该路径将包含该文件的所有生成的代码。Go软件包名称将是导入路径的最后一个路径部分。例如，我们的示例将使用包名称“tutorialpb”。

```go
option go_package = "github.com/protocolbuffers/protobuf/examples/go/tutorialpb";
```

接下来，您将需要定义消息。消息只是包含一组类型字段的汇总。许多标准的简单数据类型可用作字段类型，包括bool，int32，float，double和string。您还可以通过使用其他消息类型作为字段类型来为消息添加更多的结构。

```go
message Person {
  string name = 1;
  int32 id = 2;  // Unique ID number for this person.
  string email = 3;

  enum PhoneType {
    MOBILE = 0;
    HOME = 1;
    WORK = 2;
  }

  message PhoneNumber {
    string number = 1;
    PhoneType type = 2;
  }

  repeated PhoneNumber phones = 4;

  google.protobuf.Timestamp last_updated = 5;
}

// Our address book file is just one of these.
message AddressBook {
  repeated Person people = 1;
}
```

在上面的示例中，`Person`消息包含`PhoneNumber`消息，而`AddressBook`消息包含`Person`消息。您甚至可以定义嵌套在其他消息中的消息类型-如您所见，PhoneNumber类型是在Person内部定义的。如果您希望某个字段具有一个预定义的值列表之一，也可以定义枚举类型-在这里您要指定电话号码可以是MOBILE，HOME或WORK之一。

每个元素上的“=1”，“=2”标记标识该字段在二进制编码中使用的唯一“标记”。
标签编号1至15与较高的编号相比，编码所需的字节减少了一个字节，因此，为了进行优化，您可以决定将这些标签用于常用或重复的元素，而将标签16和更高的标签用于较少使用的可选元素。**repeated字段中的每个元素都需要重新编码标签号，因此重复字段是此优化的最佳候选者**。

如果未设置字段值，则使用默认值：数字类型为零，字符串为空字符串，布尔值为false。对于嵌入式消息，默认值始终是消息的“默认实例”或“原型”，即没有设置任何字段**。调用访问器以获取尚未显式设置的字段的值将始终返回该字段的默认值。**

如果一个字段是`repeated`，则该字段可以重复任意次（包括零次）。重复值的顺序将保留在协议缓冲区中。将重复字段视为动态大小的数组。

在[协议缓冲区语言指南](https://developers.google.com/protocol-buffers/docs/proto3)中，您将找到有关编写.proto文件的完整指南-包括所有可能的字段类型。**但是，不要去寻找类似于类继承的工具–协议缓冲区不能做到这一点**。

## 编译协议缓冲区

现在，您有了.proto，接下来需要做的是生成读取和写入AddressBook（以及Person和PhoneNumber）消息所需的类。为此，您需要在.proto上运行协议缓冲区编译器协议：

1. 如果尚未安装编译器，请[下载软件包](https://developers.google.com/protocol-buffers/docs/downloads)并按照自述文件中的说明进行操作。
2. 运行以下命令以安装Go协议缓冲区插件：
   ```go
   go install google.golang.org/protobuf/cmd/protoc-gen-go
   ```
   编译器插件`protoc-gen-go`将安装在`$GOBIN`中，默认为 `$GOPATH/bin`。它必须在`$PATH`中，这样协议编译器协议才能找到它。
3. 现在运行编译器，指定源代码目录（应用程序的源代码所在的位置；如果不提供值，则使用当前目录），目标目录（您希望生成的代码进入的位置；通常与`$SRC_DIR`相同），以及`.proto`的路径。在这种情况下，您将调用：
   ```go
   protoc -I=$SRC_DIR --go_out=$DST_DIR $SRC_DIR/addressbook.proto
   ```
4. 因为您需要Go代码，所以使用`--go_out`选项–其他受支持的语言也提供了类似的选项。

这会在您指定的目标目录中生成`github.com/protocolbuffers/protobuf/examples/go/tutorialpb/addressbook.pb.go`。

## 协议缓冲区API

生成addressbook.pb.go提供了以下有用的类型：

- 具有People字段的AddressBook结构体。
- 具有Name，Id，Email和Phones字段的Person结构体。
- 一个`Person_PhoneNumber`结构体，其中包含“Number”和“Type”字段。
- 类型`Person_PhoneType`和一个为Person.PhoneType枚举中的每个值定义的值。

您可以在[Go Generated Code指南](https://developers.google.com/protocol-buffers/docs/reference/go-generated)中详细了解确切生成的内容的详细信息，但是在大多数情况下，您可以将它们视为完全普通的Go类型。

这是`list_people`[命令的单元测试](https://github.com/protocolbuffers/protobuf/blob/master/examples/list_people_test.go)中的一个示例，该示例说明了如何创建Person实例：

```go
p := pb.Person{
        Id:    1234,
        Name:  "John Doe",
        Email: "jdoe@example.com",
        Phones: []*pb.Person_PhoneNumber{
                {Number: "555-4321", Type: pb.Person_HOME},
        },
}
```

## 写消息Message

使用协议缓冲区的全部目的是对数据进行序列化，以便可以在其他位置对其进行解析。在Go中，您使用proto库的[Marshal](https://pkg.go.dev/google.golang.org/protobuf/proto?tab=doc#Marshal)函数来序列化协议缓冲区数据。指向协议缓冲区消息结构的指针实现了proto.Message接口。调用proto.Marshal将返回协议缓冲区，并以其流格式进行编码。例如，我们在add_person命令中使用此函数：

```go
book := &pb.AddressBook{}
// ...

// Write the new address book back to disk.
out, err := proto.Marshal(book)
if err != nil {
        log.Fatalln("Failed to encode address book:", err)
}
if err := ioutil.WriteFile(fname, out, 0644); err != nil {
        log.Fatalln("Failed to write address book:", err)
}
```

## 读消息Message

要解析编码的消息，请使用proto库的Unmarshal函数。调用此方法会将buf中的数据解析为协议缓冲区，并将结果放入`pb`中。因此，要在list_people命令中解析文件，我们使用：

```go
// Read the existing address book.
in, err := ioutil.ReadFile(fname)
if err != nil {
        log.Fatalln("Error reading file:", err)
}
book := &pb.AddressBook{}
if err := proto.Unmarshal(in, book); err != nil {
        log.Fatalln("Failed to parse address book:", err)
}
```

## 扩展协议缓冲区

在发布使用协议缓冲区的代码之后，迟早无疑会希望“改进”协议缓冲区的定义。如果您希望新的缓冲区向后兼容，而旧的缓冲区向后兼容，而您几乎肯定希望这样做，那么您需要遵循一些规则。在新版本的协议缓冲区中：

- 您不得更改任何现有字段的标签号。
- 您可以删除字段。
- 您可以添加新字段，但必须使用新的标签号（即，该协议缓冲区中从未使用过的标签号，即使删除的字段的编号也不能使用）。

（这些规则有一些[例外](https://developers.google.com/protocol-buffers/docs/proto3#updating)，但很少使用。）

如果遵循这些规则，旧代码将很乐意阅读新消息，而忽略任何新字段。对于旧代码，删除的单个字段将仅具有其默认值，而删除的重复字段将为空。
新代码还将透明地读取旧消息。

但是，请记住，新字段不会出现在旧消息中，因此您需要对默认值进行合理的处理。使用特定于类型的默认值：对于字符串，默认值为空字符串。对于布尔值，默认值为false。对于数字类型，默认值为零。
