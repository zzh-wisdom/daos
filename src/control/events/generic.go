//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
)

// StrInfo contains opaque blob of type string to hold custom details for a
// generic RAS event to be forwarded through the control-plane from the
// data-plane to an external consumer e.g. syslog.
type StrInfo string

func NewStrInfo(in string) *StrInfo {
	si := StrInfo(in)
	return &si
}

func (si *StrInfo) isExtendedInfo() {}

// GetStrInfo returns extended info if of type StrInfo.
func (evt *RASEvent) GetStrInfo() *StrInfo {
	if ei, ok := evt.ExtendedInfo.(*StrInfo); ok {
		return ei
	}

	return nil
}

// StrInfoFromProto converts event info from proto to native format.
func StrInfoFromProto(pbInfo *sharedpb.RASEvent_StrInfo) (*StrInfo, error) {
	si := StrInfo(pbInfo.StrInfo)

	return &si, nil
}

// StrInfoToProto converts event info from native to proto format.
func StrInfoToProto(si *StrInfo) (*sharedpb.RASEvent_StrInfo, error) {
	pbInfo := &sharedpb.RASEvent_StrInfo{
		StrInfo: string(*si),
	}

	return pbInfo, nil
}
