//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"net"
	"sync"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
)

// DomainSocketClient is the interface to a dRPC client communicating over a
// Unix Domain Socket
// DomainSocketClient是通过Unix域套接字进行通信的dRPC客户端的接口
type DomainSocketClient interface {
	sync.Locker
	IsConnected() bool
	Connect() error
	Close() error
	SendMsg(call *Call) (*Response, error)
	GetSocketPath() string
}

// domainSocketDialer is an interface that connects to a Unix Domain Socket
// 连接到Unix域套接字的接口
type domainSocketDialer interface {
	dial(socketPath string) (net.Conn, error)
}

// ClientConnection represents a client connection to a dRPC server
// 代表一个到服务器的客户端连接
// 实现DomainSocketClient接口，包含domainSocketDialer接口
type ClientConnection struct {
	sync.Mutex
	socketPath string             // Filesystem location of dRPC socket
	dialer     domainSocketDialer // Interface to connect to the socket
	conn       net.Conn           // Connection to socket，不为nil则表示连接状态
	sequence   int64              // Increment each time we send，刚连接时初始化为0
}

// IsConnected indicates whether the client connection is currently active
// ClientConnection的conn不为nil则表示连接
func (c *ClientConnection) IsConnected() bool {
	return c.conn != nil
}

// Connect opens a connection to the internal Unix Domain Socket path
// 一个客户端只有一个连接，如果已经连接则不做任何事情。
func (c *ClientConnection) Connect() error {
	if c.IsConnected() {
		// Nothing to do
		return nil
	}

	conn, err := c.dialer.dial(c.socketPath)
	if err != nil {
		return errors.Wrap(err, "dRPC connect")
	}

	c.conn = conn
	c.sequence = 0 // reset message sequence number on connect
	return nil
}

// Close shuts down the connection to the Unix Domain Socket
func (c *ClientConnection) Close() error {
	if !c.IsConnected() {
		// Nothing to do
		return nil
	}

	err := c.conn.Close()
	if err != nil {
		return errors.Wrap(err, "dRPC close")
	}

	c.conn = nil
	return nil
}

// 通过conn发送一个Call调用
func (c *ClientConnection) sendCall(msg *Call) error {
	// increment sequence every call, always nonzero
	c.sequence++
	msg.Sequence = c.sequence

	callBytes, err := proto.Marshal(msg)
	if err != nil {
		return errors.Wrap(err, "failed to marshal dRPC request")
	}

	if _, err := c.conn.Write(callBytes); err != nil {
		return errors.Wrap(err, "dRPC send")
	}

	return nil
}

// 通过conn接受一个返回
func (c *ClientConnection) recvResponse() (*Response, error) {
	respBytes := make([]byte, MaxMsgSize)
	numBytes, err := c.conn.Read(respBytes)
	if err != nil {
		return nil, errors.Wrap(err, "dRPC recv")
	}

	resp := &Response{}
	err = proto.Unmarshal(respBytes[:numBytes], resp)
	if err != nil {
		return nil, errors.Wrap(err, "failed to unmarshal dRPC response")
	}

	return resp, nil
}

// SendMsg sends a message to the connected dRPC server, and returns the
// response to the caller.
func (c *ClientConnection) SendMsg(msg *Call) (*Response, error) {
	if !c.IsConnected() {
		return nil, errors.Errorf("dRPC not connected")
	}

	if msg == nil {
		return nil, errors.Errorf("invalid dRPC call")
	}

	err := c.sendCall(msg)
	if err != nil {
		return nil, errors.WithStack(err)
	}

	return c.recvResponse()
}

// GetSocketPath returns client dRPC socket file path.
func (c *ClientConnection) GetSocketPath() string {
	return c.socketPath
}

// NewClientConnection creates a new dRPC client
// 创建一个新的dRPC客户端
// @param socket Unix域套接字的路径
func NewClientConnection(socket string) *ClientConnection {
	return &ClientConnection{
		socketPath: socket,
		dialer:     &clientDialer{},
	}
}

// clientDialer is the concrete implementation of the domainSocketDialer
// interface for dRPC clients
// 空结构体，只是为了实现domainSocketDialer接口
type clientDialer struct {
}

// dial connects to the real unix domain socket located at socketPath
// dial连接到位于socketPath的真正的Unix域套接字
func (c *clientDialer) dial(socketPath string) (net.Conn, error) {
	addr := &net.UnixAddr{
		Net:  "unixpacket",
		Name: socketPath,
	}
	return net.DialUnix("unixpacket", nil, addr)
}
