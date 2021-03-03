//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"os"
	"path/filepath"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	defaultRetryAfter = 250 * time.Millisecond
)

type daosStatusResp struct {
	Status int32 `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
}

func (dsr *daosStatusResp) String() string {
	return ""
}

func (dsr *daosStatusResp) Reset() {}

func (dsr *daosStatusResp) ProtoMessage() {}

type retryableDrpcReq struct {
	proto.Message
	RetryAfter        time.Duration  // 若值为0，则使用默认的重试时间间隔defaultRetryAfter
	// 指定返回哪些错误状态时可以重试，这里的状态只特定于方法的返回结果，即body中检测出的错误状态
	// drpc框架返回的错误状态不能重试，即respons中的status必须为success才有可能重试
	RetryableStatuses []drpc.DaosStatus
}

func (rdr *retryableDrpcReq) GetMessage() proto.Message {
	return rdr.Message
}

// isRetryable tests the request to see if it is already wrapped
// in a retryableDrpcReq, or if it is a known-retryable request
// type. In the latter case, the incoming request is wrapped and
// returned.
func isRetryable(msg proto.Message) (*retryableDrpcReq, bool) {
	// NB: This list of retryable types is a convenience to reduce
	// boilerplate. It's still possible to set custom retry behavior
	// by manually wrapping a request before calling makeDrpcCall().
	switch msg := msg.(type) {
	case *retryableDrpcReq:
		return msg, true
	}

	return nil, false
}

func getDrpcServerSocketPath(sockDir string) string {
	return filepath.Join(sockDir, "daos_server.sock")
}

func checkDrpcClientSocketPath(socketPath string) error {
	if socketPath == "" {
		return errors.New("socket path empty")
	}

	f, err := os.Stat(socketPath)
	if err != nil {
		return errors.Errorf("socket path %q could not be accessed: %s",
			socketPath, err.Error())
	}

	if (f.Mode() & os.ModeSocket) == 0 {
		return errors.Errorf("path %q is not a socket",
			socketPath)
	}

	return nil
}

// checkSocketDir verifies socket directory exists, has appropriate permissions
// and is a directory. SocketDir should be created during configuration management
// as locations may not be user creatable.
// checkSocketDir验证套接字目录是否存在、具有适当的权限、是目录。
// 应该在配置管理期间创建SocketDir，因为位置可能不是用户可创建的。
func checkSocketDir(sockDir string) error {
	f, err := os.Stat(sockDir)
	if err != nil {
		msg := "unexpected error locating"
		if os.IsPermission(err) {
			msg = "permissions failure accessing"
		} else if os.IsNotExist(err) {
			msg = "missing"
		}

		return errors.WithMessagef(err, "%s socket directory %s", msg, sockDir)
	}
	if !f.IsDir() {
		return errors.Errorf("path %s not a directory", sockDir)
	}

	return nil
}

// drpc服务器设置请求
type drpcServerSetupReq struct {
	log     logging.Logger
	sockDir string
	engines []*EngineInstance
	tc      *security.TransportConfig
	sysdb   *system.Database
	events  *events.PubSub
}

// drpcServerSetup specifies socket path and starts drpc server.
// 指定套接字路径并启动drpc服务器。
// 1. 清除先前的套接字
// 2. 新建服务器套接字实例DomainSocketServer
// 3. 注册模块
// 4. 启动服务器
func drpcServerSetup(ctx context.Context, req *drpcServerSetupReq) error {
	// Clean up any previous execution's sockets before we create any new sockets
	// 清除所有先前执行使用的套接字
	if err := drpcCleanup(req.sockDir); err != nil {
		return err
	}

	sockPath := getDrpcServerSocketPath(req.sockDir)
	drpcServer, err := drpc.NewDomainSocketServer(ctx, req.log, sockPath)
	if err != nil {
		return errors.Wrap(err, "unable to create socket server")
	}

	// Create and add our modules
	drpcServer.RegisterRPCModule(NewSecurityModule(req.log, req.tc))
	drpcServer.RegisterRPCModule(newMgmtModule())
	drpcServer.RegisterRPCModule(newSrvModule(req.log, req.sysdb, req.engines, req.events))

	if err := drpcServer.Start(); err != nil {
		return errors.Wrapf(err, "unable to start socket server on %s", sockPath)
	}

	return nil
}

// drpcCleanup deletes any DAOS sockets in the socket directory
// 删除套接字目录中的所有DAOS套接字，即daos使用的套接字的名称是固定的，每次执行都要清除先前使用过的套接字
// 其他程序使用的套接字不会清除
func drpcCleanup(sockDir string) error {
	if err := checkSocketDir(sockDir); err != nil {
		return err
	}

	srvSock := getDrpcServerSocketPath(sockDir)
	os.Remove(srvSock)

	pattern := filepath.Join(sockDir, "daos_engine*.sock")
	engineSocks, err := filepath.Glob(pattern) // 只有pattern错误才出现error
	if err != nil {
		return errors.WithMessage(err, "couldn't get list of engine sockets")
	}

	for _, s := range engineSocks {
		os.Remove(s)
	}

	return nil
}

// checkDrpcResponse checks for some basic formatting errors
// 检查返回drpc.Response的一些基本格式错误
// 是对最原始的返回响应作判断
// 非空且Status为Status_SUCCESS
func checkDrpcResponse(drpcResp *drpc.Response) error {
	if drpcResp == nil {
		return errors.Errorf("dRPC returned no response")
	}

	if drpcResp.Status != drpc.Status_SUCCESS {
		return errors.Errorf("bad dRPC response status: %v",
			drpcResp.Status.String())
	}

	return nil
}

// newDrpcCall creates a new drpc Call instance for specified with
// the protobuf message marshalled in the body
// 创建一个新的drpc Call实例，然后指定它的body为传入bodyMessage被编组后的字节串
func newDrpcCall(method drpc.Method, bodyMessage proto.Message) (*drpc.Call, error) {
	var bodyBytes []byte
	if bodyMessage != nil {
		var err error
		bodyBytes, err = proto.Marshal(bodyMessage)
		if err != nil {
			return nil, err
		}
	}

	return &drpc.Call{
		Module: method.Module().ID(),
		Method: method.ID(),
		Body:   bodyBytes,
	}, nil
}

// makeDrpcCall opens a drpc connection, sends a message with the
// protobuf message marshalled in the body, and closes the connection.
// drpc response is returned after basic checks.
// 执行一个drpc调用，得到返回结果
// 实际上会不断重试发送调用，直到调用成功，每次重试执行以下过程：
// 1. 打开一个drpc连接
// 2. 发送一条Call消息
// 3. 关闭连接
// 4. 将返回的响应进行基本检查后，返回
// 返回单额错误有：创建Call失败、不能连接到服务器、发送消息失败、返回无效的response
func makeDrpcCall(ctx context.Context, log logging.Logger, client drpc.DomainSocketClient, method drpc.Method, body proto.Message) (drpcResp *drpc.Response, err error) {
	tryCall := func(msg proto.Message) (*drpc.Response, error) {
		client.Lock()
		defer client.Unlock()

		drpcCall, err := newDrpcCall(method, msg)
		if err != nil {
			return nil, errors.Wrap(err, "build drpc call")
		}

		// Forward the request to the I/O server via dRPC
		if err = client.Connect(); err != nil {
			if te, ok := errors.Cause(err).(interface{ Temporary() bool }); ok {
				if !te.Temporary() { // 不是暂时的，则说明是数据平面没有启动
					return nil, FaultDataPlaneNotStarted
				}
			}
			return nil, errors.Wrap(err, "connect to client")
		}
		defer client.Close()

		if drpcResp, err = client.SendMsg(drpcCall); err != nil {
			return nil, errors.Wrap(err, "send message")
		}

		if err = checkDrpcResponse(drpcResp); err != nil {
			return nil, errors.Wrap(err, "validate response")
		}

		return drpcResp, nil
	}

	if rdr, ok := isRetryable(body); ok {
		for {
			retryable := false
			drpcResp, err = tryCall(rdr.GetMessage())
			if err != nil {
				return nil, err
			}

			dsr := new(daosStatusResp)
			// 只解码状态字段的信息
			if uErr := proto.Unmarshal(drpcResp.Body, dsr); uErr != nil {
				return
			}
			status := drpc.DaosStatus(dsr.Status)

			for _, retryableStatus := range rdr.RetryableStatuses {
				if status == retryableStatus {
					retryable = true
					break
				}
			}

			if !retryable {
				return
			}

			retryAfter := rdr.RetryAfter
			if retryAfter == 0 {
				retryAfter = defaultRetryAfter
			}

			log.Infof("method %s: retryable %s; retrying after %s", method, status, retryAfter)
			select {
			case <-ctx.Done():
				log.Errorf("method %s; %s", method, ctx.Err())
				return nil, ctx.Err()
			case <-time.After(retryAfter):
				continue
			}
		}
	}

	return tryCall(body)
}
