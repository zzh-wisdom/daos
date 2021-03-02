//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"context"
	"net"
	"os"
	"sync"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

// MaxMsgSize is the maximum drpc message size that may be sent.
// Using a packetsocket over the unix domain socket means that we receive
// a whole message at a time without knowing its size. So for this reason
// we need to restrict the maximum message size so we can preallocate a
// buffer to put all of the information in. Corresponding C definition is
// found in include/daos/drpc.h
//
const MaxMsgSize = 16384

// DomainSocketServer is the object that listens for incoming dRPC connections,
// maintains the connections for sessions, and manages the message processing.
// 代表一个服务器实例
// 一个连接创建一个会话，一个会话使用一个单独的go协程监听并处理
type DomainSocketServer struct {
	log           logging.Logger
	sockFile      string  // socket路径
	ctx           context.Context
	cancelCtx     func()
	listener      net.Listener
	service       *ModuleService  // 模块服务，便于管理
	sessions      map[net.Conn]*Session  // 一个连接一个会话
	sessionsMutex sync.Mutex  // 互斥操作会话映射map
}

// closeSession cleans up the session and removes it from the list of active
// sessions.
// 关闭会话s，并将它从map中删除
func (d *DomainSocketServer) closeSession(s *Session) {
	d.sessionsMutex.Lock()
	s.Close()
	delete(d.sessions, s.Conn)
	d.sessionsMutex.Unlock()
}

// listenSession runs the listening loop for a Session. It listens for incoming
// dRPC calls and processes them.
// 循环监听会话，接受并处理客户端的调用
func (d *DomainSocketServer) listenSession(s *Session) {
	for {
		if err := s.ProcessIncomingMessage(); err != nil {
			d.closeSession(s)
			break
		}
	}
}

// Listen listens for incoming connections on the UNIX domain socket and
// creates individual sessions for each one.
// 在UNIX域套接字上侦听传入连接，并为每个会话创建单独的会话。
func (d *DomainSocketServer) Listen() {
	go func() {
		<-d.ctx.Done()
		d.log.Debug("Quitting listener")
		d.listener.Close()
	}()

	for {
		conn, err := d.listener.Accept()
		if err != nil {
			// If we're shutting down anyhow, don't print connection errors.
			if d.ctx.Err() == nil {
				d.log.Errorf("%s: failed to accept connection: %v", d.sockFile, err)
			}
			return
		}

		c := NewSession(conn, d.service)
		d.sessionsMutex.Lock()
		d.sessions[conn] = c
		d.sessionsMutex.Unlock()
		go d.listenSession(c)
	}
}

// Start sets up the dRPC server socket and kicks off the listener goroutine.
// 设置dRPC服务器套接字并启动侦听器goroutine。
func (d *DomainSocketServer) Start() error {
	// Just in case an old socket file is still lying around
	// 执行unlink()函数并不一定会真正的删除文件，它先会检查文件系统中此文件的连接数是否为1，如果不是1说明此文件还有其他链接对象，
	// 因此只对此文件的连接数进行减1操作。若连接数为1，并且在此时没有任何进程打开该文件，此内容才会真正地被删除掉。、
	// 在有进程打开此文件的情况下，则暂时不会删除，直到所有打开该文件的进程都结束时文件就会被删除。
	// 因此这个if语句的逻辑就是，如果unlink后该文件仍然存在，则说明有别的程序还在使用该socket
	if err := syscall.Unlink(d.sockFile); err != nil && !os.IsNotExist(err) {
		return errors.Wrapf(err, "Unable to unlink %s", d.sockFile)
	}

	addr := &net.UnixAddr{Name: d.sockFile, Net: "unixpacket"}
	lis, err := net.ListenUnix("unixpacket", addr)
	if err != nil {
		return errors.Wrapf(err, "Unable to listen on unix socket %s", d.sockFile)
	}
	d.listener = lis

	// TODO: Should we set more granular permissions? The only writer should
	// be the IO server and we should know which user is running it.
	// 这里设置了完全的权限，注释的意思是实际上只需要写权限
	if err := os.Chmod(d.sockFile, 0777); err != nil {
		return errors.Wrapf(err, "Unable to set permissions on %s", d.sockFile)
	}

	go d.Listen()
	return nil
}

// Shutdown places the state of the server to shutdown which terminates the
// Listen go routine and starts the cleanup of all open connections.
// 关闭监听者后，各个会话的协程会自动关闭，因为接受不到数据，从而发生错误，导致协程关闭
func (d *DomainSocketServer) Shutdown() {
	d.cancelCtx()
}

// RegisterRPCModule takes a Module and associates it with the given
// DomainSocketServer so it can be used to process incoming dRPC calls.
func (d *DomainSocketServer) RegisterRPCModule(mod Module) {
	d.service.RegisterModule(mod)
}

// NewDomainSocketServer returns a new unstarted instance of a
// DomainSocketServer for the specified unix domain socket path.
// 新建一个socket服务器
// @param sock Unix域套接字的路径
func NewDomainSocketServer(ctx context.Context, log logging.Logger, sock string) (*DomainSocketServer, error) {
	if sock == "" {
		return nil, errors.New("Missing Argument: sockFile")
	}
	service := NewModuleService(log)
	sessions := make(map[net.Conn]*Session)
	dssCtx, cancelCtx := context.WithCancel(ctx)
	return &DomainSocketServer{
		log:       log,
		sockFile:  sock,
		ctx:       dssCtx,
		cancelCtx: cancelCtx,
		service:   service,
		sessions:  sessions}, nil
}

// Session represents an individual client connection to the Domain Socket Server.
// 对于一个客户端的连接，以及该连接需要调用的模块服务。
type Session struct {
	Conn net.Conn
	mod  *ModuleService
}

// ProcessIncomingMessage listens for an incoming message on the session,
// calls its handler, and sends the response.
// 接受消息，调用相应的方法，返回响应
func (s *Session) ProcessIncomingMessage() error {
	buffer := make([]byte, MaxMsgSize)

	bytesRead, err := s.Conn.Read(buffer)
	if err != nil {
		// This indicates that we have reached a bad state
		// for the connection and we need to terminate the handler.
		return err
	}

	response, err := s.mod.ProcessMessage(s, buffer[:bytesRead])
	if err != nil {
		// The only way we hit here is if we fail to marshal the module's
		// response. Should not actually be possible. ProcessMessage
		// will generate a valid Response structure for any bad input.
		return err
	}

	_, err = s.Conn.Write(response)
	if err != nil {
		// This should only happen if we're shutting down while
		// trying to send our response.
		return err
	}

	return nil
}

// Close closes the session
// 关闭会话只用关闭连接conn
func (s *Session) Close() {
	_ = s.Conn.Close()
}

// NewSession creates a new dRPC Session object
func NewSession(conn net.Conn, svc *ModuleService) *Session {
	return &Session{
		Conn: conn,
		mod:  svc,
	}
}
