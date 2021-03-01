# Language Guide

原文：<https://developers.google.com/protocol-buffers/docs/overview#generating>

本指南描述了如何使用协议缓冲语言protocol buffer来构建您的协议缓冲区数据，包括 `.proto` 文件语法以及如何从您的 `.proto` 文件中生成数据访问类。<font color=red>它涵盖了协议缓冲语言的 **proto2** 版本</font>：有关 **proto3** 语法的信息，请参阅 Proto3 语言指南。

这是一个**参考指南** - 对于使用本文档中描述的许多功能的一步一步示例，请参阅所选语言的[教程](https://developers.google.com/protocol-buffers/docs/tutorials)。

## 定义消息类型

首先，让我们来看看一个非常简单的例子。假设您想要定义`搜索请求`消息格式，其中每个搜索请求都有查询字符串、您感兴趣结果的特定页面数以及每个页面匹配字符串的结果数。以下是您用于定义消息类型的 `.proto` 文件。

```go
message SearchRequest {
  required string query = 1;
  optional int32 page_number = 2;
  optional int32 result_per_page = 3;
}
```

SearchRequest 消息定义指定了三个字段（名称name/值value对），每条数据都是您希望包含在这个类型消息中的。每个字段都有一个名称name和类型type。

### 指定字段类型

在上述示例中，所有字段都是[标量类型](https://developers.google.com/protocol-buffers/docs/overview#scalar)（scalar types）：两个整数（`page_number`和`result_per_page`）和一个字符串（`query`）。但是，您还可以为字段指定复合类型，包括[枚举](https://developers.google.com/protocol-buffers/docs/overview#enum)（enumerations）和其他消息类型。

### 分配字段编号

如您所见，消息定义中的每个字段都有一个**唯一的数字**。这些数字用于识别[消息二进制格式](https://developers.google.com/protocol-buffers/docs/encoding)中的字段，**数字在消息类型使用后不应更改**。请注意，范围 1 到 15 中的字段编号需要**一个字节**进行编码，包括字段编号和字段类型（您可以在[协议缓冲区编码](https://developers.google.com/protocol-buffers/docs/encoding#structure)中了解更多有关这一点的信息）。范围 16 到 2047 中的字段编号需要**两个字节**。因此，<font color=red>您应该保留字段编号 1 到 15，以便用于非常频繁地发生消息的元素。请记住，要为将来可能添加的频繁发生的元素留出一些空间（数字编号）。</font>

### 指定字段规则

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

### 添加更多消息类型



