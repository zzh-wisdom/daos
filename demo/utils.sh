#!/usr/bin/env bash

#ACCESS_POINT=${ACCESS_POINT:-boro-46}
POOL_HOSTS=${POOL_HOSTS:-boro-46:10001}

function title() {
  LINE="==========================================="
	echo -e "\e[1m\e[94m${LINE}"
	echo "$1"
	echo -e "${LINE}\e[0m"
}

pause=1

function p() {
   if [ "$pause" == "1" ]; then
    read -r in
    if [ "$in" == "c" ]; then
        pause=0
    fi
   fi
}

function pause() {
  echo -e "\e[1m\e[94m==> $*\e[0m"
  p
}
function msg() {
   echo -e "\e[1m\e[94m*** $* ***\e[0m"
}

function run_cmd() {
  echo -e "\e[1m\e[32m"
  echo -e "\$ $1"
  echo -e "\e[0m"
  $1
  echo ""
}

function run_dmg_cmd() {
	run_cmd "dmg -l $POOL_HOSTS -i $1"
}

function run_daos_cmd() {
	run_cmd "daos $1"
}

function get_pool_svc() {
	UUID=$1
	dmg -l "$POOL_HOSTS" -i pool list | grep "$UUID" | sed -n 's/.*\s\+\([0-9]\+\)/\1/p'
}