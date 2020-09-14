//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package server

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

const (
	msgFormatErr      = "instance %d: failure formatting storage, check RPC response for details"
	msgNvmeFormatSkip = "NVMe format skipped on instance %d as SCM format did not complete"
)

// newState creates, populates and returns ResponseState in addition
// to logging any err.
func newState(log logging.Logger, status ctlpb.ResponseStatus, errMsg string, infoMsg string,
	contextMsg string) *ctlpb.ResponseState {

	state := &ctlpb.ResponseState{
		Status: status, Error: errMsg, Info: infoMsg,
	}

	if errMsg != "" {
		// TODO: is this necessary, maybe not?
		log.Error(contextMsg + ": " + errMsg)
	}

	return state
}

func (c *StorageControlService) doNvmePrepare(req *ctlpb.PrepareNvmeReq) (resp *ctlpb.PrepareNvmeResp) {
	resp = &ctlpb.PrepareNvmeResp{}
	msg := "Storage Prepare NVMe"
	_, err := c.NvmePrepare(bdev.PrepareRequest{
		HugePageCount: int(req.GetNrhugepages()),
		TargetUser:    req.GetTargetuser(),
		PCIWhitelist:  req.GetPciwhitelist(),
		ResetOnly:     req.GetReset_(),
	})

	if err != nil {
		resp.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_NVME, err.Error(), "", msg)
		return
	}

	resp.State = newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "", msg)
	return
}

func (c *StorageControlService) doScmPrepare(pbReq *ctlpb.PrepareScmReq) (pbResp *ctlpb.PrepareScmResp) {
	pbResp = &ctlpb.PrepareScmResp{}
	msg := "Storage Prepare SCM"

	scmState, err := c.GetScmState()
	if err != nil {
		pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(), "", msg)
		return
	}
	c.log.Debugf("SCM state before prep: %s", scmState)

	resp, err := c.ScmPrepare(scm.PrepareRequest{Reset: pbReq.Reset_})
	if err != nil {
		pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(), "", msg)
		return
	}

	info := ""
	if resp.RebootRequired {
		info = scm.MsgScmRebootRequired
	}
	pbResp.Rebootrequired = resp.RebootRequired

	pbResp.Namespaces = make(proto.ScmNamespaces, 0, len(resp.Namespaces))
	if err := (*proto.ScmNamespaces)(&pbResp.Namespaces).FromNative(resp.Namespaces); err != nil {
		pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_ERR_SCM, err.Error(), "", msg)
		return
	}
	pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", info, msg)

	return
}

// StoragePrepare configures SSDs for user specific access with SPDK and
// groups SCM modules in AppDirect/interleaved mode as kernel "pmem" devices.
func (c *StorageControlService) StoragePrepare(ctx context.Context, req *ctlpb.StoragePrepareReq) (*ctlpb.StoragePrepareResp, error) {
	c.log.Debug("received StoragePrepare RPC; proceeding to instance storage preparation")

	resp := &ctlpb.StoragePrepareResp{}

	if req.Nvme != nil {
		resp.Nvme = c.doNvmePrepare(req.Nvme)
	}
	if req.Scm != nil {
		resp.Scm = c.doScmPrepare(req.Scm)
	}

	return resp, nil
}

// checkModelSerial verifies we have non-null model and serial identifiers.
// Returns the concatenated identifier along with name of first empty field
// and boolean indicating whether both are populated or not.
func checkModelSerial(m string, s string) (ms, empty string, ok bool) {
	if strings.TrimSpace(m) == "" {
		empty = "model"
	} else if strings.TrimSpace(s) == "" {
		empty = "serial"
	}
	if empty != "" {
		return
	}
	ms = m + s
	ok = true

	return
}

// updateBdevOverDrpc updates the input list of controllers with new NVMe
// health stats and SMD metadata details.
//
// First map input controllers to their concatenated model+serial keys then
// retrieve metadata and health details for each SMD device (blobstore) on
// each local IO server instance and update details in input controller list.
func (srv *IOServerInstance) updateBdevOverDrpc(ctx context.Context, ctrlrs storage.NvmeControllers) error {

	smdDevs, err := srv.listSmdDevices(ctx, new(mgmtpb.SmdDevReq))
	if err != nil {
		return errors.Wrapf(err, "instance %d listSmdDevices()", srv.Index())
	}

	for _, dev := range smdDevs.Devices {
		c.log.Debugf("smd dev: %s", dev.GetUuid())

		health, err := srv.getBioHealth(ctx, &mgmtpb.BioHealthReq{
			DevUuid: dev.Uuid,
		})
		if err != nil {
			c.log.Debug("getBioHealth failed")
			return errors.Wrapf(err, "instance %d getBioHealth()",
				srv.Index())
		}

		c.log.Debugf("health response: %v", health)
		modelSerial, emptyField, ok := checkModelSerial(health.Model, health.Serial)
		if !ok {
			c.log.Debugf("skipping health stats for uuid %s, %s id empty",
				health.DevUuid, emptyField)
			continue
		}

		msg := fmt.Sprintf("smd info received for ctrlr model/serial %s from smd uuid %s",
			modelSerial, dev.GetUuid())

		ctrlr, exists := ctrlrMap[modelSerial]
		if !exists {
			c.log.Debug(msg + " didn't match any known controllers")
			continue
		}

		c.log.Debugf("%s->%s", msg, ctrlr.PciAddr)

		// multiple updates for the same key expected when
		// more than one controller namespaces (and resident
		// blobstores) exist, stats will be the same for each
		if err := convert.Types(health, ctrlr.HealthStats); err != nil {
			return errors.Wrapf(err, "converting health for controller %s %s",
				modelSerial, ctrlr.PciAddr)
		}

		smdDev := new(storage.SmdDevice)
		if err := convert.Types(dev, smdDev); err != nil {
			return errors.Wrapf(err, "converting smd details for controller %s %s",
				modelSerial, ctrlr.PciAddr)
		}

		ctrlr.SmdDevices = append(ctrlr.SmdDevices, smdDev)
	}

	return nil
}

func mapCtrlrsByModelSerial(ctrlrs storage.NvmeControllers) (map[string]*storage.NvmeController, error) {
	ctrlrMap := make(map[string]*storage.NvmeController) // ctrlr model+serial key

	for _, ctrlr := range ctrlrs {
		modelSerial, emptyField, ok := checkModelSerial(ctrlr.Model, ctrlr.Serial)
		if !ok {
			return nil, errors.Errorf("input controller %s is missing %s identifier",
				ctrlr.PciAddr, emptyField)
		}

		if _, exists := ctrlrMap[modelSerial]; exists {
			return nil, errors.Errorf("duplicate entries for controller %s, key %s",
				ctrlr.PciAddr, modelSerial)
		}

		ctrlrMap[modelSerial] = ctrlr
	}

	return ctrlrMap, nil
}

func (c *ControlService) scanInstanceBdevs(ctx context.Context, configDevicesOnly bool) (storage.NvmeControllers, error) {
	// use cached controller details by default
	bdevReq := bdev.ScanRequest{Rescan: false}

	if !configDevicesOnly {
		bsr, err := c.NvmeScan(bdevReq)
		if err != nil {
			return nil, errors.Wrap(err, "NvmeScan()")
		}

		return bsr.Controllers, nil
	}

	var allCtrlrs storage.NvmeControllers
	instances := c.harness.Instances()
	// only retrieve results for devices listed in server config
	for _, srv := range instances {
		instanceBdevs := c.instanceStorage[srv.Index()].Bdev.GetNvmeDevs()
		bdevReq.DeviceList = append(bdevReq.DeviceList, instanceBdevs...)
		c.log.Debugf("instance %d storage scan: only show bdev devices in config %v",
			srv.Index(), instanceBdevs)

		// rescan through control-plane to get up-to-date stats if io
		// servers are not active (and therefore have not claimed the
		// assigned devices)
		if !srv.isReady() {
			bdevReq.Rescan = true
		}

		bsr, err := c.NvmeScan(bdevReq)
		if err != nil {
			return nil, errors.Wrap(err, "NvmeScan()")
		}

		if bdevReq.Rescan {
			allCtrlrs = append(allCtrlrs, bsr.Controllers...)
			continue
		}

		ctrlrMap, err := mapCtrlrsByModelSerial(bsr.Controllers)
		if err != nil {
			return nil, errors.Wrap(err, "mapCtrlrsByModelSerial()")
		}

		// if io servers are active and have claimed the assigned devices,
		// query over drpc to update controller details with current health
		// stats and smd info
		if err := srv.nvmeUpdateOverDrpc(ctx, ctrlrMap); err != nil {
			return nil, errors.Wrap(err, "updating bdev health and smd info")
		}

		allCtrlrs = append(allCtrlrs, bsr.Controllers...)
	}

	return allCtrlrs, nil
}

func (c *ControlService) scanBdevs(ctx context.Context, configDevicesOnly bool, resp *ctlpb.ScanNvmeResp) error {
	state := newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "",
		"Scan NVMe Storage")

	ctrlrs, err := c.scanInstanceBdevs(ctx, configDevicesOnly)
	if err != nil {
		state.Status = ctlpb.ResponseStatus_CTL_ERR_NVME
		state.Error = err.Error()
		resp.State = state

		return nil
	}

	pbCtrlrs := make(proto.NvmeControllers, 0, len(ctrlrs))
	if err := pbCtrlrs.FromNative(ctrlrs); err != nil {
		return errors.Wrapf(err, "convert %#v to protobuf format", ctrlrs)
	}

	resp.State = state
	resp.Ctrlrs = pbCtrlrs

	return nil
}

func (c *ControlService) scanInstanceScm(ctx context.Context) (storage.ScmNamespaces, storage.ScmModules, error) {
	// rescan scm storage details by default
	scmReq := scm.ScanRequest{Rescan: true}
	ssr, err := c.ScmScan(scmReq)
	if err != nil {
		return nil, nil, err
	}

	return ssr.Namespaces, ssr.Modules, nil
}

func (c *ControlService) scanScm(ctx context.Context, resp *ctlpb.ScanScmResp) error {
	state := newState(c.log, ctlpb.ResponseStatus_CTL_SUCCESS, "", "",
		"Scan SCM Storage")

	namespaces, modules, err := c.scanInstanceScm(ctx)
	if err != nil {
		state.Status = ctlpb.ResponseStatus_CTL_ERR_SCM
		state.Error = err.Error()
		resp.State = state

		return nil
	}

	if len(namespaces) > 0 {
		resp.Scm.Namespaces = make(proto.ScmNamespaces, 0, len(namespaces))
		err := (*proto.ScmNamespaces)(&resp.Scm.Namespaces).FromNative(namespaces)
		if err != nil {
			return errors.Wrapf(err, "convert %#v to protobuf format", namespaces)
		}
		resp.State = state

		return nil
	}

	resp.Scm.Modules = make(proto.ScmModules, 0, len(modules))
	if err := (*proto.ScmModules)(&resp.Scm.Modules).FromNative(modules); err != nil {
		return errors.Wrapf(err, "convert %#v to protobuf format", modules)
	}
	resp.State = state

	return nil
}

// StorageScan discovers non-volatile storage hardware on node.
func (c *ControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	c.log.Debug("received StorageScan RPC")
	resp := new(ctlpb.StorageScanResp)
	resp.Nvme = new(ctlpb.ScanNvmeResp)
	resp.Scm = new(ctlpb.ScanScmResp)

	if err := c.scanBdevs(ctx, req.ConfigDevicesOnly, resp.Nvme); err != nil {
		return nil, err
	}

	resp.Scm = new(ctlpb.ScanScmResp)
	if err := c.scanPmems(ctx, req.ConfigDevicesOnly, resp.Scm); err != nil {
		return nil, err
	}

	c.log.Debug("responding to StorageScan RPC")

	return resp, nil
}

// StorageFormat delegates to Storage implementation's Format methods to prepare
// storage for use by DAOS data plane.
//
// Errors returned will stop other servers from formatting, non-fatal errors
// specific to particular device should be reported within resp results instead.
//
// Send response containing multiple results of format operations on scm mounts
// and nvme controllers.
func (c *ControlService) StorageFormat(ctx context.Context, req *ctlpb.StorageFormatReq) (*ctlpb.StorageFormatResp, error) {
	instances := c.harness.Instances()
	resp := new(ctlpb.StorageFormatResp)
	resp.Mrets = make([]*ctlpb.ScmMountResult, 0, len(instances))
	resp.Crets = make([]*ctlpb.NvmeControllerResult, 0, len(instances))
	scmChan := make(chan *ctlpb.ScmMountResult, len(instances))

	c.log.Debugf("received StorageFormat RPC %v; proceeding to instance storage format", req)

	// TODO: enable per-instance formatting
	formatting := 0
	for _, srv := range instances {
		formatting++
		go func(s *IOServerInstance) {
			scmChan <- s.StorageFormatSCM(req.Reformat)
		}(srv)
	}

	instanceErrored := make(map[uint32]bool)
	for formatting > 0 {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case scmResult := <-scmChan:
			formatting--
			if scmResult.GetState().GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
				instanceErrored[scmResult.GetInstanceidx()] = true
			}
			resp.Mrets = append(resp.Mrets, scmResult)
		}
	}

	// TODO: perform bdev format in parallel
	for _, srv := range instances {
		if instanceErrored[srv.Index()] {
			// if scm errored, indicate skipping bdev format
			if len(srv.bdevConfig().DeviceList) > 0 {
				resp.Crets = append(resp.Crets,
					srv.newCret("", ctlpb.ResponseStatus_CTL_SUCCESS, "",
						fmt.Sprintf(msgNvmeFormatSkip, srv.Index())))
			}
			continue
		}
		// SCM formatted correctly on this instance, format NVMe
		cResults := srv.StorageFormatNVMe(c.bdev)
		if cResults.HasErrors() {
			instanceErrored[srv.Index()] = true
		}
		resp.Crets = append(resp.Crets, cResults...)
	}

	// Notify storage ready for instances formatted without error.
	// Block until all instances have formatted NVMe to avoid
	// VFIO device or resource busy when starting IO servers
	// because devices have already been claimed during format.
	// TODO: supply whitelist of instance.Devs to init() on format.
	for _, srv := range instances {
		if instanceErrored[srv.Index()] {
			srv.log.Errorf(msgFormatErr, srv.Index())
			continue
		}
		srv.NotifyStorageReady()
	}

	return resp, nil
}
