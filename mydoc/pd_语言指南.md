# Language Guide

- [1. 定义消息类型](#1-定义消息类型)
  - [1.1. 指定字段类型](#11-指定字段类型)
  - [1.2. 分配字段编号](#12-分配字段编号)
  - [1.3. 指定字段规则](#13-指定字段规则)
  - [1.4. 添加更多消息类型](#14-添加更多消息类型)
  - [1.5. 添加注释](#15-添加注释)
  - [1.6. 保留字段](#16-保留字段)
  - [1.7. `.proto`产生了什么？](#17-proto产生了什么)
- [2. 标量Scalar值类型](#2-标量scalar值类型)
- [3. 可选字段和默认值](#3-可选字段和默认值)
- [4. 枚举](#4-枚举)
  - [4.1. 保留值](#41-保留值)
- [5. 使用其他消息类型](#5-使用其他消息类型)
  - [5.1. 导入定义](#51-导入定义)
  - [5.2. 使用proto3消息类型](#52-使用proto3消息类型)
- [6. 嵌套类型](#6-嵌套类型)
  - [6.1. Groups](#61-groups)
- [7. 更新Updating消息类型](#7-更新updating消息类型)
- [扩展 Extensions](#扩展-extensions)
  - [嵌套扩展](#嵌套扩展)
  - [选择扩展编号](#选择扩展编号)
- [Oneof](#oneof)
  - [使用Oneof](#使用oneof)
  - [Oneof功能](#oneof功能)
  - [向后兼容性问题](#向后兼容性问题)
    - [标记重复使用问题](#标记重复使用问题)
- [Maps](#maps)
  - [map的功能](#map的功能)
  - [向后兼容性](#向后兼容性)
- [Packages](#packages)
  - [包和名称解释Resolution](#包和名称解释resolution)
- [定义服务](#定义服务)
- [Options](#options)
  - [自定义选项](#自定义选项)
- [生成您的类](#生成您的类)
- [8. 更多阅读](#8-更多阅读)

原文：<https://developers.google.com/protocol-buffers/docs/overview>

本指南描述了如何使用协议缓冲语言protocol buffer来构建您的协议缓冲区数据，包括 `.proto` 文件语法以及如何从您的 `.proto` 文件中生成数据访问类。<font color=red>它涵盖了协议缓冲语言的 **proto2** 版本</font>：有关 **proto3** 语法的信息，请参阅 Proto3 语言指南。

这是一个**参考指南** - 对于使用本文档中描述的许多功能的一步一步示例，请参阅所选语言的[教程](https://developers.google.com/protocol-buffers/docs/tutorials)。

## 1. 定义消息类型

首先，让我们来看看一个非常简单的例子。假设您想要定义`搜索请求`消息格式，其中每个搜索请求都有查询字符串、您感兴趣结果的特定页面编号以及每个页面匹配字符串的结果数。以下是您用于定义消息类型的 `.proto` 文件。

```go
message SearchRequest {
  required string query = 1;
  optional int32 page_number = 2;
  optional int32 result_per_page = 3;
}
```

SearchRequest 消息定义指定了三个字段（名称name/值value对），每条数据都是您希望包含在这个类型消息中的。每个字段都有一个名称name和类型type。

### 1.1. 指定字段类型

在上述示例中，所有字段都是[标量类型](https://developers.google.com/protocol-buffers/docs/overview#scalar)（scalar types）：两个整数（`page_number`和`result_per_page`）和一个字符串（`query`）。但是，您还可以为字段指定复合类型，包括[枚举](https://developers.google.com/protocol-buffers/docs/overview#enum)（enumerations）和其他消息类型。

### 1.2. 分配字段编号

如您所见，消息定义中的每个字段都有一个**唯一的数字**。这些数字用于识别[消息二进制格式](https://developers.google.com/protocol-buffers/docs/encoding)中的字段，**数字在消息类型使用后不应更改**。请注意，范围 1 到 15 中的字段编号需要**一个字节**进行编码，包括字段编号和字段类型（您可以在[协议缓冲区编码](https://developers.google.com/protocol-buffers/docs/encoding#structure)中了解更多有关这一点的信息）。范围 16 到 2047 中的字段编号需要**两个字节**。因此，<font color=red>您应该保留字段编号 1 到 15，以便用于非常频繁地发生消息的元素。请记住，要为将来可能添加的频繁发生的元素留出一些空间（数字编号）。</font>

### 1.3. 指定字段规则

您可以指定的规则如下：

- `required`：一个正确的消息必须拥有该字段，且只有1个。
- `optional`：可以有0或1个该字段，但不能超过1个。
- `repeated`：此字段可以重复任意次数（包括0次）。**重复的值的顺序将保留**。

由于历史原因，标量数字类型的重复字段的编码效率不如他们的可能（即没能最大程度的编码）。新代码应使用特殊选项[`packed=true`]，以获得更高效的编码效率。例如：

```go
repeated int32 samples = 4 [packed=true];
```

您可以在[协议缓冲区编码](https://developers.google.com/protocol-buffers/docs/encoding#packed)中了解有关`packed`编码的更多详细信息。

你需要非常小心地标记字段为`required`。**如果您希望在某个时刻停止编写或发送某个`required`字段，将字段更改为`optional`字段将是有问题的** - 旧读者会认为没有此字段的消息是不完整的，并可能无意中拒绝或丢弃它们。您应该考虑为您的缓冲区编写特定于应用程序的**自定义验证程序**。**谷歌的一些工程师得出的结论是，使用`required`弊大于利：他们更喜欢只使用`optional`和`repeated`**。然而，这种观点并不普遍，还需要根据实际情况选择。

### 1.4. 添加更多消息类型

可以在单个`.proto`文件中定义多种消息类型。如果要定义多个相关消息，这很有用–例如，如果要定义与`SearchRequest`消息类型相对应的答复消息格式`SearchResponse`，可以将其添加到相同的`.proto`文件中：

```go
message SearchRequest {
  required string query = 1;
  optional int32 page_number = 2;
  optional int32 result_per_page = 3;
}

message SearchResponse {
 ...
}
```

**合并消息会导致膨胀**。虽然可以在单个.proto文件中定义多种消息类型（例如消息message、枚举enum和服务service），但是当在单个文件中定义大量具有不同依赖性的消息时，也会**导致依赖性膨胀**。<font color=red><b>建议每个`.proto`文件尽可能少地包含消息类型。</b></font>

### 1.5. 添加注释

要将注释添加到`.proto`文件中，请使用C/C ++样式 `//` 和 `/*...*/` 语法。

```go
/* SearchRequest represents a search query, with pagination options to
 * indicate which results to include in the response. */

message SearchRequest {
  required string query = 1;
  optional int32 page_number = 2;  // Which page number do we want?
  optional int32 result_per_page = 3;  // Number of results to return per page.
}
```

### 1.6. 保留字段

如果您通过**完全删除字段**或**将其注释掉**来[更新](https://developers.google.com/protocol-buffers/docs/overview#updating)消息类型，则将来的用户在自己对该类型进行更新时可以**重用该字段的编号**。如果他们以后加载同一`.proto`的旧版本，可能会导致严重的问题，包括数据损坏，隐私错误等。确保不会发生这种情况的一种方法是指定已删除字段的字段编号（和/或名称，这也可能导致JSON序列化问题）为`reserved`。协议缓冲区编译器将**通过比较**来判断将来是否有任何用户尝试使用这些字段标识符，。

```go
message Foo {
  reserved 2, 15, 9 to 11;
  reserved "foo", "bar";
}
```

<font color=red>请注意，您不能在同一 `reserved` 语句中混合使用字段名和字段号。</font>

### 1.7. `.proto`产生了什么？

在`.proto`上运行[协议缓冲区编译器](https://developers.google.com/protocol-buffers/docs/overview#generating)时，编译器会以您选择的语言生成代码，您将需要使用生成的文件来处理文件中描述的消息类型，包括获取和设置字段值，将消息序列化为输出流，并从输入流中解析消息。

- 对于C++，编译器会从每个.proto 生成一个.h 和.cc 文件，并为文件中描述的每种消息类型提供一个**类**。
- 对于Java，编译器会生成一个 .java 文件，其中包含每种消息类型的**类**以及用于创建消息类实例的特殊**Builder类**。
- Python稍有不同-Python编译器会在.proto中生成带有每种消息类型的静态描述符的模块，然后将该模块与元类*metaclass*一起使用，以在运行时创建必要的Python数据访问类。
- 对于Go，编译器会生成一个.pb.go文件，其中包含文件中每种消息类型的类型。

您可以按照所选语言的教程，找到有关每种语言使用API​​的更多信息。有关API的更多详细信息，请参阅相关的[API参考](https://developers.google.com/protocol-buffers/docs/reference/overview)。

## 2. 标量Scalar值类型

标量消息字段可以具有以下类型之一-该表显示.proto文件中指定的类型，以及自动生成的类中的相应类型：

| .proto Type | Notes                                                        | C++ Type | Java Type          | Python Type[2]                       | Go Type  |
| :---------- | :----------------------------------------------------------- | :------- | :----------------- | :----------------------------------- | :------- |
| double      |                                                              | double   | double             | float                                | *float64 |
| float       |                                                              | float    | float              | float                                | *float32 |
| int32       | 使用**变长编码**。**负数编码效率低下**–如果您的字段可能具有负值，请改用`sint32`。 | int32    | int                | int                                  | *int32   |
| int64       | 使用**变长编码**。**负数编码效率低下**–如果您的字段可能具有负值，请改用`sint64`。 | int64    | long               | int/long<sup>[3]</sup>               | *int64   |
| uint32      | 使用**变长编码**。                                           | uint32   | int<sup>[1]</sup>  | int/long<sup>[3]</sup>               | *uint32  |
| uint64      | 使用**变长编码**。                                           | uint64   | long<sup>[1]</sup> | int/long<sup>[3]</sup>               | *uint64  |
| sint32      | 使用**变长编码**。**有符号的int值**。与常规int32相比，它们更有效地对负数进行编码。 | int32    | int                | int                                  | *int32   |
| sint64      | 使用**变长编码**。**有符号的int值**。与常规int64相比，它们更有效地编码负数。 | int64    | long               | int/long<sup>[3]</sup>               | *int64   |
| fixed32     | 始终为四个字节。**如果值通常大于2<sup>28</sup>，则比uint32更有效。** | uint32   | int<sup>[1]</sup>  | int/long<sup>[3]</sup>               | *uint32  |
| fixed64     | 始终为八个字节。**如果值通常大于2<sup>56</sup>，则比uint64更有效。** | uint64   | long<sup>[1]</sup> | int/long<sup>[3]</sup>               | *uint64  |
| sfixed32    | 始终为四个字节。                                             | int32    | int                | int                                  | *int32   |
| sfixed64    | 始终为八个字节。                                             | int64    | long               | int/long<sup>[3]</sup>               | *int64   |
| bool        |                                                              | bool     | boolean            | bool                                 | *bool    |
| string      | 字符串必须始终包含UTF-8编码或7位ASCII文本。                  | string   | String             | unicode (Python 2) or str (Python 3) | *string  |
| bytes       | 可以包含任意字节序列。                                       | string   | ByteString         | bytes                                | []byte   |

在[协议缓冲区编码](https://developers.google.com/protocol-buffers/docs/encoding)中序列化消息时，您可以找到有关这些类型如何编码的更多信息。

原表参考[这里](https://developers.google.com/protocol-buffers/docs/overview#scalar)。

[1] 在Java中，无符号的32位和64位整数使用带符号的对等体(分别为int和long)表示，最高位仅存储在符号位中。

[2] 在所有情况下，将设置字段值时将执行**类型检查**以确保其有效。

[3] 64位或无符号32位整数在解码时始终表示为long，但是如果在设置字段时指定了int，则可以为int。在所有情况下，该值都必须适合设置时表示的类型。
参见[2]。

## 3. 可选字段和默认值

如上所述，消息描述中的元素可以标记为`optional`。格式正确的消息可能包含也可能不包含可选元素。**解析消息时，如果其中不包含可选元素，则解析对象中的相应字段将设置为该字段的默认值**。可以将默认值指定为消息描述的一部分。例如，假设您要为SearchRequest的result_per_page值提供默认值10。

```go
optional int32 result_per_page = 3 [default = 10];
```

如果未为可选元素指定默认值，则使用特定于类型的默认值：**对于字符串，默认值为空字符串。对于字节，默认值为空字节字符串。对于布尔值，默认值为false。对于数字类型，默认值为零。对于枚举，默认值为枚举类型定义中列出的第一个值。**

**这意味着在将值添加到枚举值列表的开头时必须格外小心。**有关如何安全地更改定义的准则，请参阅“[更新消息类型](https://developers.google.com/protocol-buffers/docs/overview#updating)”部分。

## 4. 枚举

定义消息类型时，您可能希望其字段之一仅具有一个预定义的值列表之一。例如，假设您要为每个SearchRequest添加一个 `corpus` 字段，该 corpus 可以是UNIVERSAL，WEB，IMAGES，LOCAL，NEWS，PRODUCTS或VIDEO。您可以通过在消息定义中添加枚举来非常简单地执行此操作-**枚举类型的字段只能使用一组指定的常量作为其值（如果尝试提供其他值，则解​​析器将其视为未知字段）**。在下面的示例中，我们添加了一个名为Corpus的枚举，其中包含所有可能的值以及一个Corpus类型的字段：

```go
message SearchRequest {
  required string query = 1;
  optional int32 page_number = 2;
  optional int32 result_per_page = 3 [default = 10];
  enum Corpus {
    UNIVERSAL = 0;
    WEB = 1;
    IMAGES = 2;
    LOCAL = 3;
    NEWS = 4;
    PRODUCTS = 5;
    VIDEO = 6;
  }
  optional Corpus corpus = 4 [default = UNIVERSAL];
}
```

**您可以通过将相同的值分配给不同的枚举常量来定义别名。**为此，您需要将**`allow_alia`s选项设置为`true`**，否则协议编译器将在找到别名时生成错误消息。

```go
enum EnumAllowingAlias {
  option allow_alias = true;
  UNKNOWN = 0;
  STARTED = 1;
  RUNNING = 1;
}
enum EnumNotAllowingAlias {
  UNKNOWN = 0;
  STARTED = 1;
  // RUNNING = 1;  // Uncommenting this line will cause a compile error inside Google and a warning message outside.
}
```

**枚举常量必须在32位整数范围内。**<font color=red>由于枚举值在传输时使用varint编码，因此负值效率不高，因此不建议使用</font>。您可以在消息定义内定义枚举，如上面的示例所示，也可以在外部定义-这些枚举可以在.proto文件中的任何消息定义中重复使用。您还可以使用语法`_MessageType_._EnumType_`将一条消息中声明的枚举类型用作另一条消息中的字段类型。

在使用枚举的.proto上运行协议缓冲区编译器时，生成的代码将具有一个对应的Java或C++枚举，或者一个用于Python的特殊EnumDescriptor类，该类用于在运行时生成的类中创建一组带有整数值的符号常量。

:information_source: **注意**：生成的代码可能会受到语言特定的枚举数限制（一种语言的成千上万个）。请查看您计划使用的语言的限制。

有关如何在应用程序中使用消息枚举的更多信息，请参见针对所选语言[生成的代码指南](https://developers.google.com/protocol-buffers/docs/reference/overview)。

### 4.1. 保留值

如果您通过完全删除枚举条目或将其注释掉来更新枚举类型，则将来的用户在自己对类型进行更新时可以重复使用数值。如果他们以后加载同一.proto的旧版本，可能会导致严重的问题，包括数据损坏，隐私错误等。确保不会发生这种情况的一种方法是指定已删除条目的数字值（和/或名称这也可能导致JSON序列化问题）为 `reserved`。如果将来有任何用户尝试使用这些标识符，则协议缓冲区编译器会进行complain（报错或警告）。您可以使用`max`关键字指定保留的数值范围达到最大可能值。

```go
enum Foo {
  reserved 2, 15, 9 to 11, 40 to max;
  reserved "FOO", "BAR";
}
```

请注意，您不能在同一保留语句中混合使用字段名和数字值。

## 5. 使用其他消息类型

您可以使用其他消息类型作为字段类型。例如，假设您想在每`SearchResponse`消息中包括`Result`消息–为此，您可以在同一`.proto`中定义`Result`消息类型，然后在SearchResponse中指定`Result`类型的字段：

```go
message SearchResponse {
  repeated Result result = 1;
}

message Result {
  required string url = 1;
  optional string title = 2;
  repeated string snippets = 3;
}
```

### 5.1. 导入定义

在上面的示例中，`Result` 消息类型与SearchResponse定义在同一文件中-如果要在另一个.proto文件中定义要用作字段类型的消息类型，该怎么办？

您可以通过导入其他`.proto`文件使用它们的定义。要导入另一个`.proto`的定义，请在文件顶部添加一个import语句：

```go
import "myproject/other_protos.proto";
```

默认情况下，您只能使用直接导入的`.proto`文件中的定义。但是，**有时您可能需要将.proto文件移动到新位置**。现在，您可以直接在原始位置放置一个**虚拟.proto文件**，进而使用 `import public` 概念将所有导入转发到新位置，而不是直接移动.proto文件并一次更改所有引用站点。导入包含 `import public` 声明的proto文件的任何人都可以**可传递地**依赖 `import public` 依赖项。例如：

```go
// new.proto
// All definitions are moved here
```

```go
// old.proto
// This is the proto that all clients are importing.
import public "new.proto";
import "other.proto";
```

```go
// client.proto
import "old.proto";
// You use definitions from old.proto and new.proto, but not other.proto
```

协议编译器使用 `-I/--proto_path` 标志在协议编译器命令行中指定的**一组目录**中搜索导入的文件。**如果未给出标志，它将在调用编译器的目录中查找。**<font color=red>通常，应将`--proto_path`标志设置为项目的根目录，并对所有导入使用<b>完全限定的名称</b></font>。

### 5.2. 使用proto3消息类型

可以导入proto3消息类型并在proto2消息中使用它们，反之亦然。
**但是，不能在proto3语法中使用proto2枚举。**

## 6. 嵌套类型

您可以在其他消息类型中**定义**和使用消息类型，如以下示例所示–在SearchResponse消息中定义了Result消息：

```go
message SearchResponse {
  message Result {
    required string url = 1;
    optional string title = 2;
    repeated string snippets = 3;
  }
  repeated Result result = 1;
}
```

如果要在其父消息类型之外重用此消息类型，则将其称为`_Parent_._Type_`：

```go
message SomeOtherMessage {
  optional SearchResponse.Result result = 1;
}
```

您可以根据需要深度嵌套消息：

```go
message Outer {       // Level 0
  message MiddleAA {  // Level 1
    message Inner {   // Level 2
      required int64 ival = 1;
      optional bool  booly = 2;
    }
  }
  message MiddleBB {  // Level 1
    message Inner {   // Level 2
      required int32 ival = 1;
      optional bool  booly = 2;
    }
  }
}
```

### 6.1. Groups

:warning:**请注意，此功能已弃用，在创建新的消息类型时不应使用–而是使用嵌套消息类型。**

组是在消息定义中嵌套信息的另一种方法。例如，另一种指定包含多个结果的SearchResponse的方法如下：

```go
message SearchResponse {
  repeated group Result = 1 {
    required string url = 2;
    optional string title = 3;
    repeated string snippets = 4;
  }
}
```

组将嵌套的消息类型和字段简单地组合到一个声明中。在您的代码中，您可以将此消息视为具有一个称为result的Result类型字段（前者的名称将转换为小写，以使其与后者不冲突）。因此，此示例与上面的SearchResponse完全等效，除了该消息具有不同的[连线格式（编码格式）](https://developers.google.com/protocol-buffers/docs/encoding)。

## 7. 更新Updating消息类型

如果现有消息类型不再满足您的所有需求（例如，您希望消息格式具有一个额外的字段），但是您仍然希望使用以旧格式创建的代码，请不要担心！**在不破坏任何现有代码的情况下更新消息类型非常简单**。只要记住以下规则：

- 不要更改任何现有字段的字段**编号**。
- 您添加的任何新字段都应该是 `optional` 或 `repeated`。这意味着任何使用“旧”消息格式通过代码序列化的消息都可以由新生成的代码解析，因为它们不会缺少任何必需的元素。您应该为这些元素设置合理的默认值，以便新代码可以与旧代码生成的消息正确交互。同样，由新代码创建的消息可以由旧代码解析：**旧的二进制文件在解析时只会忽略新字段**。<font color=red>但是，未知字段不会被丢弃，并且如果消息随后被序列化，则未知字段也会与之一起进行序列化–因此，如果消息传递给新代码，则新字段仍然可用。</font>
- 只要在更新后的消息类型中不再使用该字段编号，就可以删除非`required`的字段。您可能想要重命名该字段，或者添加前缀“OBSOLETE_”，或者设置该字段编号为`reserved`，以使`.proto`的将来用户不会意外重用该编号。
- 只要类型和数字保持不变，就可以将非必须字段转换为[extension](https://developers.google.com/protocol-buffers/docs/overview#extensions)，反之亦然。
- `int32`，`uint32`，`int64`，`uint64`和`bool`都是兼容的–这意味着您可以将字段从这些类型中的一种更改为另一种，而不会破坏向前或向后的兼容性。如果从流中解析出的一个数字不适合对应的类型，则将获得与在C++中将数字强制转换为该类型一样的效果（例如，如果将64位数字读为int32，它将被截断为32位）。
- `sint32`和`sint64`彼此兼容，但与其他整数类型不兼容。
- 只要字节是有效的UTF-8，字符串和字节都是兼容的。
- 如果字节包含消息的编码版本，则嵌入式消息类型与字节`bytes`兼容。
- fixed32与sfixed32兼容，fixed64与sfixed64兼容。
- 对于字符串、字节和消息字段，`optional` 与 `repeated` 兼容。对于给定重复字段的序列化数据作为输入，期望此字段是**可选**的客户端将采用最后一个输入值如果它是**原始类型**字段；或者将合并所有输入元素如果它是**消息类型**字段。请注意，这对于数字类型（包括布尔值和枚举）通常并不安全。重复的数字类型字段可以以打包格式序列化，当期望使用可选字段时，该格式将无法正确解析。
- 对于字符串、字节和消息字段，`optional` 与重复兼容。给定重复字段的序列化数据作为输入，如果期望此字段是可选的，则如果它是原始类型字段，则将采用最后一个输入值；如果是消息类型字段，则将合并所有输入元素。请注意，这对于数字类型（包括布尔值和枚举）通常并**不安全**。重复的数字类型字段可以以[打包](https://developers.google.com/protocol-buffers/docs/encoding#packed)格式序列化，当期望得到可选字段时，该格式将无法正确解析。
- 只要您记得从未通过网络发送默认值，就可以更改默认值。因此，如果程序收到未设置特定字段的消息，则该程序将看到该程序的协议版本中定义的默认值。它不会看到在发送者的代码中定义的默认值。
- enum在编码格式方面与int32，uint32，int64和uint64兼容（**请注意，如果值不合适，它们将被截断**），但是请注意，在反序列化消息时，客户端代码可能会以不同的方式对待它们。值得注意的是，对消息进行反序列化时，**无法识别的枚举值将被丢弃，这会使字段的has..访问方法返回false，并且其getter返回枚举定义中列出的第一个值；如果指定了默认值，则返回默认值**。对于重复的枚举字段，所有无法识别的值将从列表中删除。然而，整数字段将始终保留其值。**因此，在将整数升级为枚举时，您需要非常小心，因为它会在线路上接收超出范围的枚举值。**
- 在当前的Java和C++实现中，当无法识别的枚举值被去除时，它们会与其他未知字段一起存储。**请注意，如果将此数据序列化然后由识别这些值的客户端重新解析，则可能导致奇怪的行为。**对于可选字段，即使在反序列化原始消息后写入了新值，识别该值的客户端仍会读取旧值。在重复字段的情况下，旧值将出现在任何已识别和新添加的值之后，这意味着将**不保留顺序**。
- 将单个可选值更改为**新**的`oneof`的成员是安全且二进制兼容的（即更改为嵌入消息类型中的一个成员）。如果您确定没有代码一次set多个字段，则将多个可选字段移动到一个新的`oneof`字段中可能是安全的。将任何字段移动到现有的`oneof`字段中都是不安全的。
- 在`map<K, V>`和相应的重复消息字段之间更改字段是二进制兼容的（有关消息布局和其他限制，请参见下面的[Maps](https://developers.google.com/protocol-buffers/docs/overview#maps)）。但是，更改的安全性取决于应用程序：在反序列化和重新序列化消息时，使用重复字段定义的客户端将产生语义上相同的结果；**但是，使用映射字段定义的客户端可以对条目进行重新排序，并删除具有重复键的条目**。

## 扩展 Extensions

扩展Extensions使您可以声明消息中可用于第三方扩展的字段号范围。扩展是其类型未由原始.proto文件定义的字段的**占位符**。这允许**其他.proto文件**通过使用这些字段编号定义某些或所有字段的类型来将其添加到您的消息定义中。让我们看一个例子：

```go
message Foo {
  // ...
  extensions 100 to 199;
}
```

这表示Foo中的字段号的[100，199]范围是为扩展保留的。现在，其他用户可以使用您指定范围内的字段编号在自己的.proto文件（该文件导入了你的.proto文件）中将新字段添加到Foo中，例如：

```go
extend Foo {
  optional int32 bar = 126;
}
```

这会在Foo的原始定义中添加一个名为bar的字段，其字段号为126。

对用户的Foo消息进行编码后，编码格式与用户在Foo中定义新字段的方式完全相同。但是，您在应用程序代码中**访问**扩展字段的方式与访问常规字段略有不同–您生成的数据访问代码具有用于处理扩展的特殊访问器。例如，这是在C++中设置`bar`的值的方法：

```go
Foo foo;
foo.SetExtension(bar, 15);
```

同样，Foo类定义了模板访问器`HasExtension()`，`ClearExtension()`，`GetExtension()`，`MutableExtension()`和`AddExtension()`。这些都具有与正常字段的相应生成的访问器匹配的语义。有关使用扩展的更多信息，请参见针对您选择的语言生成的代码参考。

请注意，扩展可以是任何字段类型，包括消息类型，但不能是`oneofs`或`maps`。

### 嵌套扩展

您可以在另一种类型的范围内声明扩展：

```go
message Baz {
  extend Foo {
    optional int32 bar = 126;
  }
  ...
}
```

在这种情况下，用于访问此扩展的C++代码为：

```go
Foo foo;
foo.SetExtension(Baz::bar, 15);
```

换句话说，唯一的效果是在`Baz`的范围内定义了`bar`。

这是造成混淆的常见原因：声明嵌套在消息类型内的扩展块并不意味着外部类型与扩展类型之间有任何的关系。特别地，以上示例并不意味着`Baz`是`Foo`的任何子类。上面的声明只意味着符号 `bar` 已在`Baz`范围内**声明**；它只是一个静态成员。

一种常见的模式是在扩展的字段类型范围内定义扩展-例如，这是Baz类型的Foo扩展，其中该扩展名定义为`Baz`的一部分：

```go
message Baz {
  extend Foo {
    optional Baz foo_ext = 127;
  }
  ...
}
```

但是，不需要在消息类型的内部定义具有消息类型的扩展。您也可以这样做：

```go
message Baz {
  ...
}

// This can even be in a different file.
extend Foo {
  optional Baz foo_baz_ext = 127;
}
```

实际上，为了避免混淆，可能首选此语法。如上所述，嵌套语法经常被不熟悉扩展的用户误认为是子类。

### 选择扩展编号

确保两个用户不使用**相同的字段号**将扩展添加到同一消息类型是非常重要的–如果意外将扩展解释为错误的类型，则可能导致数据损坏。您可能需要考虑为项目定义**扩展编号约定**，以防止发生这种情况。

如果您的编号约定可能涉及具有非常大字段编号的扩展，则可以使用 `max` 关键字指定您的扩展范围可达到最大可能的字段编号：

```go
message Foo {
  extensions 1000 to max;
}
```

`max` is 229 - 1, or 536,870,911.

与通常选择字段编号一样，您的编号约定还需要避免字段编号 19000 到 19999（`FieldDescriptor::kFirstReservedNumber` 到 `FieldDescriptor::kLastReservedNumber`），因为它们保留给协议缓冲区实现的。您可以定义包含此范围的扩展范围，但**协议编译器不允许您使用这些数字定义实际扩展**。

## Oneof

如果您有一个带有许多可选字段的消息，并且**同时**只能设置最多一个字段，则可以通过使用`oneof`功能执行此行为并节约内存。

`Oneof`字段就像可选字段，但排除oneof共享内存中的所有字段，并且最多只能同时设置一个字段。设置oneof的任何成员将自动清除所有其他成员。您可以根据您选择的语言，使用`case()`或`WhichOneof()`方法检查oneof中的哪一个值设置了（如果有）。

### 使用Oneof

要在`.proto`中定义oneof，请使用`oneof`关键字，然后使用您的oneof名称，在这种情况下，为test_oneof：

```go
message SampleMessage {
  oneof test_oneof {
     string name = 4;
     SubMessage sub_message = 9;
  }
}
```

然后，将你的oneof字段添加到`oneof`定义中。您可以添加任何类型的字段，但不能使用`required`、`optional`或`repeated`关键字。如果您需要向oneof添加重复字段，则可以使用包含重复字段的消息了类型。

在生成的代码中，oneof字段具有与常规`optional`方法相同的获取器和设置器。您还可以获得一种特殊方法，用于检查其中一个值（如果有）是否设置。您可以在相关的 [API 参考](https://developers.google.com/protocol-buffers/docs/reference/overview)中为您选择的语言找到更多有关 oneof 的 API。

### Oneof功能

- 设置一个oneof字段将自动清除其中的所有其他成员。因此，如果您设置了几个oneof字段，则只有最后一个设置的字段才具有值。

    ```go
    SampleMessage message;
    message.set_name("name");
    CHECK(message.has_name());
    message.mutable_sub_message();   // Will clear name field.
    CHECK(!message.has_name());
    ```

- 如果解析器在流上遇到同一个oneof的多个成员，则只有**最后一个**成员在解析消息中使用。
- oneof不支持扩展。
- oneof不能为重复的。
- 反射API适用于oneof字段。
- 如果将一个oneof字段设置为默认值（例如将一个int32的oneof字段设置为 0），则该字段的"case"将被设置，并且该值将在流上序列化。
- 如果您正在使用C++，**请确保您的代码不会导致内存崩溃**。以下示例代码将崩溃，因为sub_message已通过调用set_name()方法将其删除。

    ```go
    SampleMessage message;
    SubMessage* sub_message = message.mutable_sub_message();
    message.set_name("name");      // Will delete sub_message
    sub_message->set_...            // Crashes here
    ```

- 同样，在C++中，如果您将两条消息的oneofs `Swap()`，则每条消息最终都会得到另一条消息的oneof值：在下面的示例中，`msg1` 将有一个`sub_message`，`msg2` 将有一个`name`。

    ```go
    SampleMessage msg1;
    msg1.set_name("name");
    SampleMessage msg2;
    msg2.mutable_sub_message();
    msg1.swap(&msg2);
    CHECK(msg1.has_sub_message());
    CHECK(msg2.has_name());
    ```

### 向后兼容性问题 

添加或删除oneof字段时要小心。如果检查oneof的值返回`None/NOT_SET`，则可能意味着oneof未设置或已设置为不同版本的字段。没有办法区分，因为没有办法知道流上的未知字段是否为oneof的成员。

#### 标记重复使用问题

- **将可选字段移入或移出oneof**：在消息序列化和解析后，您可能会丢失一些信息（某些字段将被清除）。**但是，您可以安全地将（外部的）单个字段移动到新的oneof中，如果已知只有一个字段曾经设置过，则可以移动多个字段。**
- **删除一个oneof字段然后将其添加回来**：这可能会在消息序列化和解析后清除您当前设置的字段之一。
- **拆分或合并oneof**：这与移动常规`optional`字段有相似的问题。

## [Maps](https://developers.google.com/protocol-buffers/docs/overview#maps)

如果要创建关联映射作为数据定义的一部分，则协议缓冲区提供了一种方便的快捷方式语法：

```go
map<key_type, value_type> map_field = N;
```

其中`key_type`可以是任何整数或字符串类型（因此，可以是除了**浮点类型**和**字节**之外的任何[标量类型](https://developers.google.com/protocol-buffers/docs/overview#scalar)）。请注意，**枚举**不是有效的`key_type`。`value_type`可以是除了另一个映射map以外的任何类型。

因此，例如，如果您想创建一个项目projects的映射，其中每个Project消息都与一个字符串键key相关联，则可以这样定义它：

```go
map<string, Project> projects = 3;
```

生成的map API当前可用于所有proto2支持的语言。您可以在相关[API参考](https://developers.google.com/protocol-buffers/docs/reference/overview)中找到有关所选语言的map API的更多信息。

### map的功能

- map不支持扩展
- map不能是repeated, optional, or required。
- map值的迭代顺序和编码顺序是未定义的。因此，您不能依赖map条目按特定顺序排列。
- 为.proto生成文本格式时，map按key进行排序。数字key按照数值进行排序。
- 当从流解释时，或合并时，**如果有重复的map键，则使用最后一个看到的key**。从文本格式解析map时，如果存在重复key，解析可能会失败。

### 向后兼容性

不支持map的语言可以用repeated的消息字段代替。

（更多信息参考原文档）

## Packages

您可以在`.proto`文件中添加可选的 `package` 指定器，以防止协议消息类型之间的命名冲突。

```go
package foo.bar;
message Open { ... }
```

然后，您可以在定义消息类型字段时使用包指定器：

```go
message Foo {
  ...
  required foo.bar.Open open = 1;
  ...
}
```

包指定器影响生成代码的方式取决于您选择的语言：

- 在C++生成的类被包裹在C++名称空间中。例如，`Open`将在名称空间 `foo::bar`。
- 在 Java 中，该包用作 Java 包，除非您在.proto 文件中明确提供`option java_package`。
- 在 Python 中，包指令被忽略，因为 Python 模块是根据其在文件系统中的位置组织的。
- 在 Go 中，包指令被忽略，生成的 `.pb.go` 文件位于以相应的go_proto_library规则命名的包中。

请注意，即使包指令不直接影响生成的代码，例如在 Python 中，仍强烈建议为 .proto 文件指定包，否则可能导致描述符中的命名冲突，并使proto文件不适合其他语言。

### 包和名称解释Resolution

在协议缓冲语言中键入名称解析，如C++：首先搜索最内层的范围，然后搜索次内层等等，每个包都被认为是其父包的"内在"。前导的'.'（例如，`.foo.bar.Baz`）是指从最外层的范围开始。

协议缓冲器编译器通过解析导入的.proto文件来解决所有类型名称。每个语言的代码生成器知道如何在该语言中指定每个类型，即使它具有不同的范围规则。

## 定义服务

如果您想使用带有 RPC（Remote Procedure Call）系统的消息类型，您可以在 .proto 文件中定义 RPC 服务接口，协议缓冲器编译器将生成所选语言中的服务接口代码和存根stub。因此，例如，如果您想定义一个使用`SearchRequest`并返回`SearchResponse`的 RPC 服务，则可以在您的`.proto` 文件中将其定义如下：

```go
service SearchService {
  rpc Search(SearchRequest) returns (SearchResponse);
}
```

默认情况下，协议编译器将生成一个名为`SearchService`的抽象接口和相应的`stub`实现。存根stub将所有调用转发到 `RpcChannel`，而 `RpcChannel` 又是一个抽象接口，您必须根据自己的 RPC 系统来定义自己的RpcChannel。例如，您可以实现 `RpcChannel`，该通道将消息序列化并通过 HTTP 发送到服务器。换句话说，生成的存根为基于协议缓冲区的 RPC 调用提供了一个类型安全的接口，而不会将您锁定到任何特定的 RPC 实现中。因此，在C++中，您最终可能会编写这样的代码：

```go
using google::protobuf;

protobuf::RpcChannel* channel;
protobuf::RpcController* controller;
SearchService* service;
SearchRequest request;
SearchResponse response;

void DoSearch() {
  // You provide classes MyRpcChannel and MyRpcController, which implement
  // the abstract interfaces protobuf::RpcChannel and protobuf::RpcController.
  channel = new MyRpcChannel("somehost.example.com:1234");
  controller = new MyRpcController;

  // The protocol compiler generates the SearchService class based on the
  // definition given above.
  service = new SearchService::Stub(channel);

  // Set up the request.
  request.set_query("protocol buffers");

  // Execute the RPC.
  service->Search(controller, request, response, protobuf::NewCallback(&Done));
}

void Done() {
  delete service;
  delete channel;
  delete controller;
}
```

所有服务类都实现了`Service`接口，它提供了一种在编译时不知道方法名称或输入和输出类型的情况下调用特定方法的途径。在服务器方，这可用于实现可以注册服务的 RPC 服务器。

```go
using google::protobuf;

class ExampleSearchService : public SearchService {
 public:
  void Search(protobuf::RpcController* controller,
              const SearchRequest* request,
              SearchResponse* response,
              protobuf::Closure* done) {
    if (request->query() == "google") {
      response->add_result()->set_url("http://www.google.com");
    } else if (request->query() == "protocol buffers") {
      response->add_result()->set_url("http://protobuf.googlecode.com");
    }
    done->Run();
  }
};

int main() {
  // You provide class MyRpcServer.  It does not have to implement any
  // particular interface; this is just an example.
  MyRpcServer server;

  protobuf::Service* service = new ExampleSearchService;
  server.ExportOnPort(1234, service);
  server.Run();

  delete service;
  return 0;
}
```

如果您不想插入自己现有的 RPC 系统，您现在可以使用 [gRPC](https://github.com/grpc/grpc-common)：在 Google 开发的语言和平台中立的开源 RPC 系统。gRPC 在协议缓冲器方面效果特别好，可让您使用特殊的协议缓冲器编译器插件直接从 `.proto` 文件中生成相关的 RPC 代码。但是，由于使用 proto2 和 proto3 生成的客户端和服务器之间存在潜在的兼容性问题，我们建议您使用 proto3 来定义 gRPC 服务。您可以在 [proto3 语言指南](https://developers.google.com/protocol-buffers/docs/proto3)中了解更多有关 proto3 语法的知识。如果您确实想将 proto2 与 gRPC 一起使用，您需要使用版本 3.0.0 或更高的协议缓冲器编译器和库。

除了 gRPC 之外，还有一些正在进行的第三方项目，用于开发用于协议缓冲区的 RPC 实现。有关我们了解的项目链接列表，请参阅[第三方附加组件 wiki 页面](https://github.com/protocolbuffers/protobuf/blob/master/docs/third_party.md)。

## Options

有些选项是文件级选项，这意味着它们应写在顶层范围，而不是任何消息、列举或服务定义内。有些选项是消息级选项，这意味着它们应该写在消息定义中。有些选项是字段级选项，这意味着它们应写在字段定义内。选项也可以写在列举类型、列举值、oneof字段、服务类型和服务方法上；但是，目前不存在任何用于这些方面的有用选项。

### 自定义选项

协议缓冲区甚至允许您定义和使用您自己的选项。请注意，这是大多数人不需要的高级功能。由于选项由 `google/protobuf/descriptor.proto` 中定义的消息定义（如`FileOptions`或`FieldOptions`），定义您自己的选项只是[扩展](https://developers.google.com/protocol-buffers/docs/overview#extensions)这些消息的问题。例如：

```go
import "google/protobuf/descriptor.proto";

extend google.protobuf.MessageOptions {
  optional string my_option = 51234;
}

message MyMessage {
  option (my_option) = "Hello world!";
}
```

在这里，我们通过扩展`MessageOptions`定义了一个新的消息级别选项。然后，当我们使用该选项时，选项名必须附在括号中，以表示它是扩展。我们现在可以在这样的C++中阅读my_option的值：

```go
string value = MyMessage::descriptor()->options().GetExtension(my_option);
```

自定义选项可以定义为Protocol Buffers语言中的每种类型构造。下面是一个使用各种选项的示例：

```go
import "google/protobuf/descriptor.proto";

extend google.protobuf.FileOptions {
  optional string my_file_option = 50000;
}
extend google.protobuf.MessageOptions {
  optional int32 my_message_option = 50001;
}
extend google.protobuf.FieldOptions {
  optional float my_field_option = 50002;
}
extend google.protobuf.OneofOptions {
  optional int64 my_oneof_option = 50003;
}
extend google.protobuf.EnumOptions {
  optional bool my_enum_option = 50004;
}
extend google.protobuf.EnumValueOptions {
  optional uint32 my_enum_value_option = 50005;
}
extend google.protobuf.ServiceOptions {
  optional MyEnum my_service_option = 50006;
}
extend google.protobuf.MethodOptions {
  optional MyMessage my_method_option = 50007;
}

option (my_file_option) = "Hello world!";

message MyMessage {
  option (my_message_option) = 1234;

  optional int32 foo = 1 [(my_field_option) = 4.5];
  optional string bar = 2;
  oneof qux {
    option (my_oneof_option) = 42;

    string quux = 3;
  }
}

enum MyEnum {
  option (my_enum_option) = true;

  FOO = 1 [(my_enum_value_option) = 321];
  BAR = 2;
}

message RequestType {}
message ResponseType {}

service MyService {
  option (my_service_option) = FOO;

  rpc MyMethod(RequestType) returns(ResponseType) {
    // Note:  my_method_option has type MyMessage.  We can set each field
    //   within it using a separate "option" line.
    option (my_method_option).foo = 567;
    option (my_method_option).bar = "Some string";
  }
}
```

## 生成您的类

要生成Java、Python或C++代码，您需要处理 `.proto` 文件中定义的消息类型，您需要在 .proto 上运行协议缓冲器编译器 `protoc`。如果您尚未安装编译器，请下载该包并按照 README 中的说明操作。

协议编译器的调用命令如下：

```go
protoc --proto_path=IMPORT_PATH --cpp_out=DST_DIR --java_out=DST_DIR --python_out=DST_DIR path/to/file.proto
```

- `IMPORT_PATH`指定解析 `import` 指令时在其中查找.proto文件的目录。
如果省略，则使用当前目录。可以通过多次传递--proto_path选项来指定多个导入目录。将按顺序搜索它们。`-IIMPORT_PATH`可以用作`--proto_path`的缩写形式。
- 您可以提供一个或多个输出指令：
  - `--cpp_out`在DST_DIR中生成C++代码。有关更多信息，请参见[C++生成的代码参考](https://developers.google.com/protocol-buffers/docs/reference/cpp-generated)。
  - `--java_out`在DST_DIR中生成Java代码。有关更多信息，请参见[Java生成的代码参考](https://developers.google.com/protocol-buffers/docs/reference/java-generated)。
  - `--python_out`在DST_DIR中生成Python代码。详情请参见[Python生成的代码参考](https://developers.google.com/protocol-buffers/docs/reference/python-generated)。为进一步方便起见，如果DST_DIR以.zip或.jar结尾，则编译器会将输出写入具有给定名称的单个ZIP格式的存档文件。根据Java JAR规范的要求，还将为`.jar`输出提供清单文件。请注意，如果输出归档文件已经存在，它将被覆盖；编译器不够智能，无法将文件添加到现有存档中。
- 您必须提供**一个或多个**`.proto` 文件作为输入。可以一次指定多个.proto文件。尽管这些文件是相对于当前目录命名的，但是每个文件都必须位于`IMPORT_PATH`之一中，以便编译器可以确定其规范命名。

## 8. 更多阅读

- 指导综述：<https://developers.google.com/protocol-buffers/docs/tutorials>
- API Reference：<https://developers.google.com/protocol-buffers/docs/reference/overview>
