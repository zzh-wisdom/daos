#!/bin/bash

POOL_UUID="f00df00d-f00d-f00d-f00d-f00df00df00d"
CONT_UUID="11111111-1111-1111-1111-111111111111"
HOSTS=boro-3,boro-17
CLIENT_NP=32
ACCESS_POINT=boro-50:10001

export D_LOG_MASK=ERR
unset DD_SUBSYS
unset DD_MASK


function get_pool_svc() {
	UUID=$1
	dmg -l $ACCESS_POINT -i pool list | grep "$UUID" | sed -n 's/.*\s\+\([0-9]\+\)/\1/p'
}

function reset_server() {
  ~/workspace/go.sh stop clean start

  # wait for server to start back up
  sleep 30
}

function title() {
  LINE="==========================================="
	echo -e "\e[1m\e[94m${LINE}"
	echo "$1"
	echo -e "${LINE}\e[0m"
}

function run_cmd() {
  echo -e "\e[1m\e[32m"
  echo -e "\$ $1"
  echo -e "\e[0m"
  $1
  echo ""
}

function run_dmg_cmd() {
	run_cmd "dmg -l $ACCESS_POINT -i $1"
}

function run_daos_cmd() {
	run_cmd "daos $1"
}

function run() {
	XFER=$1
	BLOCK=$2
	PROPERTIES=$3

	OBJ_CLASS=RP_2G2 #SX

#  reset_server

	run_dmg_cmd "pool create -s 8G --pool=$POOL_UUID"
	while [[ "$SVC" == "" ]]
	do
		SVC=`get_pool_svc $POOL_UUID`
	done
	sleep 1

	run_daos_cmd "cont create --pool=$POOL_UUID --svc=$SVC --cont=$CONT_UUID $PROPERTIES"
	sleep 1



  # Verify write and read
	run_cmd "mpirun -host $HOSTS -np $CLIENT_NP ior -a DAOS -v -w -W -r -R -i 2 -o daos:testFile --daos.chunk_size 1048576 --daos.group daos_server --daos.oclass $OBJ_CLASS --daos.pool $POOL_UUID --daos.svcl $SVC --daos.cont $CONT_UUID -t $XFER -b $BLOCK "

	# Performance ...
#	run_cmd "mpirun -host $HOSTS -np $CLIENT_NP ior -a DAOS -v -w -r -i 2 -o daos:testFile --daos.chunk_size 1048576 --daos.group daos_server --daos.oclass SX --daos.pool $POOL_UUID --daos.svcl $SVC --daos.cont $CONT_UUID -t $XFER -b $BLOCK "

	echo "Delete Container & Pool"
	run_daos_cmd "cont destroy --pool=$POOL_UUID --svc=$SVC --cont=$CONT_UUID"

	run_dmg_cmd "pool destroy --pool=$POOL_UUID"

}

#title "-- 256B XFER --"
title "Checksums enabled"
run "256B" "4M" "--properties=cksum:crc16"
#title "Checksums disabled"
#run "256B" "4M" ""


#title "Checksums enabled, Server Side Verify Enabled"
#run "256B" "64M" "--properties=cksum:crc16,srv_cksum:on"

#title "-- 1M XFER --"
#title "Checksums disabled"
#run "1M" "256M" ""
#title "Checksums enabled"
#run "1M" "256M" "--properties=cksum:crc16"
#run "1M" "512M" "--properties=cksum:crc16,cksum_size:1048576"
#title "Checksums enabled, Server Side Verify Enabled"
#run "1M" "512M" "--properties=cksum:crc16,srv_cksum:on"



