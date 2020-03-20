#!/bin/bash

# TODO: figure out why checksum.c C_TRACE_ENABLED() if failing and preventing logs

## for logging
# tail -F /tmp/daos_server.log | grep --line-buffered csum | sed 's/.*\scsum\s\w*\s.*\.c:[0-9]*\s//g'
# sed 's/^.*\?\]\s//g' | sed 's/src\/.*\.c:[0-9]\+\s//g'
# tail -n0 -F /tmp/daos_server.log | stdbuf -o0 sed 's/^.*\?\]\s//g' | sed 's/src\/.*\.c:[0-9]\+\s//g'
# tail -n0 -F /tmp/daos.log | stdbuf -o0 sed 's/^.*\?\]\s//g' | sed 's/src\/.*\.c:[0-9]\+\s//g'


# To run server with NVMe
# /usr/lib64/openmpi3/bin/orterun --enable-recovery -x PATH -H boro-46 --map-by node --mca pml "ob1" --mca btl_openib_warn_default_gid_prefix "0" --mca oob "tcp" --mca plm_rsh_args "-l root -i /home/rjensen1/.ssh/id_rsa_daos_server" --mca btl "tcp,self" --np 1 daos_server -o /home/rjensen1/workspace/daos/daos_server.yml -b start -i


# make sure server is running and daos_agent is started
# Want debug logging with facility set to csum (for both server and client)
# dmg storage format -i -l boro-46 --reformat
# daos_server storage prepare --nvme-only --reset

demo_dir=$( dirname "${BASH_SOURCE[0]}" )
source $demo_dir/utils.sh

POOL_UUID="f00df00d-f00d-f00d-f00d-f00df00df00d"
CONT_UUID="11111111-1111-1111-1111-111111111111"
CSUM_CONT_UUID="22222222-2222-2222-2222-222222222222"
CSUM_SRV_CONT_UUID="33333333-3333-3333-3333-333333333333"
ARR_CONT_UUID="44444444-4444-4444-4444-444444444444"
AGG_CONT_UUID="55555555-5555-5555-5555-555555555555"

NVME_UUID1=$(dmg storage query smd --devices --pools -l=boro-46 -i | grep UUID | sed 's/\s*UUID:\s//g')
NVME_UUID2=$(dmg storage query smd --devices --pools -l=boro-47 -i | grep UUID | sed 's/\s*UUID:\s//g')

function csum_counter() {
  echo -e "\e[1m\e[32m"
  echo -e "\$ dmg -l $POOL_HOSTS -i storage query blobstore-health --devuuid=$NVME_UUID1 -l $ACCESS_POINT | grep Checksum"
  echo -e "\e[0m"
  dmg -l boro-46:10001 -i storage query blobstore-health --devuuid=$NVME_UUID1  | grep Checksum | sed 's/^\s*//g'
  dmg -l boro-47:10001 -i storage query blobstore-health --devuuid=$NVME_UUID2  | grep Checksum | sed 's/^\s*//g'
  echo ""
}

#export D_LOG_MASK=ERR
#unset DD_SUBSYS
export D_LOG_MASK=DEBUG
export DD_SUBSYS=csum

clear
pause "Creating pool and containers"

run_dmg_cmd "pool create -s 1G --pool=$POOL_UUID"
while [[ "$SVC" == "" ]]
do
	SVC=`get_pool_svc $POOL_UUID`
done

function aggregation_off {
  run_dmg_cmd "pool set-prop --pool=$POOL_UUID --name=reclaim --value=disabled"
}
function aggregation_on {
  run_dmg_cmd "pool set-prop --pool=$POOL_UUID --name=reclaim --value=time"
}

aggregation_off

msg "Container with Checksum Disabled"
run_daos_cmd "cont create --properties=cksum:off --pool=$POOL_UUID --cont=$CONT_UUID $PROPERTIE--svc=$SVC"
msg "Container with Checksum Enabled"
run_daos_cmd "cont create --properties=cksum:crc16 --cont=$CSUM_CONT_UUID $PROPERTIES --pool=$POOL_UUID --svc=$SVC"
msg "Container with Checksum Enabled and Server Side Verifiy Enabled"
run_daos_cmd "cont create --properties=cksum:crc16,srv_cksum:on --cont=$CSUM_SRV_CONT_UUID --pool=$POOL_UUID --svc=$SVC"

msg "Array Container (Checksum Enabled, Chunksize=4)"
run_daos_cmd "cont create --properties=cksum:crc16,cksum_size:4 --cont=$ARR_CONT_UUID --pool=$POOL_UUID --svc=$SVC"
msg "Aggregation Container (Checksum Enabled)"
run_daos_cmd "cont create --properties=cksum:crc32,cksum_size:16 --cont=$AGG_CONT_UUID --pool=$POOL_UUID --svc=$SVC"

title "--- Basic I/O (Single Value & Array Value) ---"
pause "I/O demo client app write/read ..."
msg "Single Value"
run_cmd "demo_client write --string Hello_World --cont=$CONT_UUID"
run_cmd "demo_client read --cont=$CONT_UUID"
pause "Array Value"
run_cmd "demo_client write --string Hello_World --index=0 --iod-type=ARRAY --akey=array --cont=$CONT_UUID"
run_cmd "demo_client read --iod-type=ARRAY --index=0 --length=11 --akey=array --cont=$CONT_UUID"

pause "Checksum Counter"
csum_counter

pause "Checksums Disabled (Corrupt on update)"
msg "Single Value"
run_cmd "demo_client write --fault 0 --string Hello_World --cont=$CONT_UUID"
pause ""
run_cmd "demo_client read --cont=$CONT_UUID"
pause "Array Value"
run_cmd "demo_client write --fault 0 --index=0 --string Hello_World --iod-type=ARRAY --akey=array --cont=$CONT_UUID"
pause ""
run_cmd "demo_client read --iod-type=ARRAY --index=0 --length=11 --akey=array --cont=$CONT_UUID"

pause "Checksums Enabled (Corrupt on update)"
msg "Single Value"
run_cmd "demo_client write --fault 0 --string Hello_World --cont=$CSUM_CONT_UUID"
pause
run_cmd "demo_client read --cont=$CSUM_CONT_UUID"
csum_counter
pause "Array Value"
run_cmd "demo_client write --fault 0 --string Hello_World --iod-type=ARRAY --akey=array --cont=$CSUM_CONT_UUID"
pause
run_cmd "demo_client read --iod-type=ARRAY --akey=array --cont=$CSUM_CONT_UUID"
csum_counter

pause "Checksums Enabled, Server Side Verify Enabled (Corrupt on update)"
run_cmd "demo_client write --fault 0 --string Hello_World --cont=$CSUM_SRV_CONT_UUID"
csum_counter
run_cmd "demo_client read --cont=$CSUM_SRV_CONT_UUID"
csum_counter
pause ""

pause "Checksums Enabled, Server Side Verify Enabled (Corrupt on disk)"
run_cmd "demo_client write --fault 0 --fault-type=DISK --string Hello_World --cont=$CSUM_SRV_CONT_UUID"
run_cmd "demo_client read --cont=$CSUM_SRV_CONT_UUID"
pause ""

pause "Checksums Enabled, Server Side Verify Enabled (Corrupt on disk), Object Replicated"
run_cmd "demo_client write --object-type=RP_2G1_SR --fault 0 --fault-type=DISK --string Hello_World --cont=$CSUM_SRV_CONT_UUID"
pause ""
run_cmd "demo_client read --object-type=RP_2G1_SR --cont=$CSUM_SRV_CONT_UUID"
csum_counter

pause ""

title "--- Array Values ---"

pause "Write 1 extent and read back"
run_cmd "demo_client write --string 12345678 --index=4 --iod-type=ARRAY --akey=array_1 --cont=$ARR_CONT_UUID"
run_cmd "demo_client read --index=4 --length=8 --iod-type=ARRAY --akey=array_1 --cont=$ARR_CONT_UUID"

pause "Write 2 extents and read back, chunk aligned"
run_cmd "demo_client write --string 12345678 --index=4 --iod-type=ARRAY --akey=array_2 --cont=$ARR_CONT_UUID"
run_cmd "demo_client write --string 9012 --index=12 --iod-type=ARRAY --akey=array_2 --cont=$ARR_CONT_UUID"
run_cmd "demo_client read --index=4 --length=12 --iod-type=ARRAY --akey=array_2 --cont=$ARR_CONT_UUID"

#pause "Partial Read from array_2"
#run_cmd "demo_client read --index=5 --length=10 --iod-type=ARRAY --akey=array_2 --cont=$ARR_CONT_UUID"

pause "Crazy ..."
run_cmd "demo_client write --string AAAAA --index=2 --iod-type=ARRAY --akey=array_3 --cont=$ARR_CONT_UUID"
run_cmd "demo_client write --string BBBB --index=4 --iod-type=ARRAY --akey=array_3 --cont=$ARR_CONT_UUID"
run_cmd "demo_client write --string CCCCCC --index=10 --iod-type=ARRAY --akey=array_3 --cont=$ARR_CONT_UUID"
run_cmd "demo_client write --string DDDDDD --index=5 --iod-type=ARRAY --akey=array_3 --cont=$ARR_CONT_UUID"
pause ""
run_cmd "demo_client read --index=2 --length=12 --iod-type=ARRAY --akey=array_3 --cont=$ARR_CONT_UUID"

pause "Checksums Enabled (Corrupt on update)"
run_cmd "demo_client write --fault 0 --string 111111111 --index=0 --iod-type=ARRAY --akey=array_4 --cont=$ARR_CONT_UUID"
run_cmd "demo_client write --string AA --index=0 --iod-type=ARRAY --akey=array_4 --cont=$ARR_CONT_UUID"
pause ""
run_cmd "demo_client read --index=0 --length=20 --iod-type=ARRAY --akey=array_4 --cont=$ARR_CONT_UUID"
csum_counter

title "Aggregation - extents fit in single chunk"
pause ""
run_cmd "demo_client write --string 111111111_ --index=0 --iod-type=ARRAY --akey=array_5 --cont=$CSUM_CONT_UUID"
run_cmd "demo_client write --string 222222222_ --index=10 --iod-type=ARRAY --akey=array_5 --cont=$CSUM_CONT_UUID"
run_cmd "demo_client write --string 333333333_ --index=20 --iod-type=ARRAY --akey=array_5 --cont=$CSUM_CONT_UUID"

msg "Reading whole array"
run_cmd "demo_client read --iod-type=ARRAY --akey=array_5 --index=0 --length=30 --cont=$CSUM_CONT_UUID"

pause "Turn Aggregation on ..."
aggregation_on
pause "Wait for aggregation to run ..."
aggregation_off
pause "Reading whole array"
run_cmd "demo_client read --iod-type=ARRAY --akey=array_5 --index=0 --length=30 --cont=$CSUM_CONT_UUID"
pause "Read Crazy"
run_cmd "demo_client read --index=2 --length=12 --iod-type=ARRAY --akey=array_3 --cont=$ARR_CONT_UUID"

pause "Insert corruption"
run_cmd "demo_client write -x0 --string 111111111_ --index=0 --iod-type=ARRAY --akey=array_6 --cont=$AGG_CONT_UUID"
run_cmd "demo_client write --string 222222222_ --index=5 --iod-type=ARRAY --akey=array_6 --cont=$AGG_CONT_UUID"
run_cmd "demo_client read --iod-type=ARRAY --akey=array_6 --index=5 --length=10 --cont=$AGG_CONT_UUID"
pause "Turn Aggregation on ..."
aggregation_on
pause "Wait for aggregation to run ..."
aggregation_off
run_cmd "demo_client read --iod-type=ARRAY --akey=array_6 --index=5 --length=10 --cont=$AGG_CONT_UUID"

pause "Clean up"
run_daos_cmd "cont destroy --pool=$POOL_UUID --cont=$CONT_UUID --svc=$SVC"
run_daos_cmd "cont destroy --pool=$POOL_UUID --cont=$CSUM_CONT_UUID --svc=$SVC"
run_daos_cmd "cont destroy --pool=$POOL_UUID --cont=$CSUM_SRV_CONT_UUID --svc=$SVC"
run_dmg_cmd "pool destroy --pool=$POOL_UUID"


