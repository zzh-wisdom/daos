#!/bin/bash

# Install basic tools
yum -y install epel-release
yum -y install \
	boost-devel clang-analyzer cmake CUnit-devel doxygen file flex \
        gcc gcc-c++ git golang graphviz lcov                           \
        libaio-devel libcmocka-devel libevent-devel libiscsi-devel     \
        libtool libtool-ltdl-devel libuuid-devel libyaml-devel         \
        make meson nasm ninja-build numactl-devel openssl-devel        \
        pandoc patch python python-devel python36-devel                \
        python-pep8 python-pygit2 python2-pygithub python-requests     \
        readline-devel scons sg3_utils ShellCheck yasm pciutils        \
        valgrind-devel python36-pylint man

# DAOS specific
yum -y install \
	python2-avocado python2-avocado-plugins-output-html \
	python2-avocado-plugins-varianter-yaml-to-mux       \
	yum-plugin-copr python-pathlib                      \
	ndctl ipmctl e2fsprogs                              \
	python2-clustershell python2-Cython python2-pip     \
	python36-clustershell python36-paramiko             \
	python36-numpy python36-jira python3-pip
yum -y copr enable jhli/ipmctl
yum -y copr enable jhli/safeclib
yum -y install libipmctl-devel openmpi3-devel hwloc-devel

# DAOS python 3 packages required for pylint
#  - excluding mpi4py as it depends on CORCI-635
pip3 install avocado-framework
pip3 install avocado-framework-plugin-result-html
pip3 install avocado-framework-plugin-varianter-yaml-to-mux
