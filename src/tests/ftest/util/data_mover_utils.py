#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

from command_utils_base import FormattedParameter
from command_utils_base import BasicParameter
from command_utils import ExecutableCommand
from job_manager_utils import Mpirun


class DcpCommand(ExecutableCommand):
    """Defines a object representing a dcp command."""

    def __init__(self, namespace, command):
        """Create a dcp Command object."""
        super(DcpCommand, self).__init__(namespace, command)

        # dcp options

        # IO buffer size in bytes (default 64MB)
        self.blocksize = FormattedParameter("--blocksize {}")
        # work size per task in bytes (default 64MB)
        self.chunksize = FormattedParameter("--chunksize {}")
        # DAOS source pool
        self.daos_src_pool = FormattedParameter("--daos-src-pool {}")
        # DAOS destination pool
        self.daos_dst_pool = FormattedParameter("--daos-dst-pool {}")
        # DAOS source container
        self.daos_src_cont = FormattedParameter("--daos-src-cont {}")
        # DAOS destination container
        self.daos_dst_cont = FormattedParameter("--daos-dst-cont {}")
        # DAOS prefix for unified namespace path
        self.daos_prefix = FormattedParameter("--daos-prefix {}")
        # read source list from file
        self.input_file = FormattedParameter("--input {}")
        # copy original files instead of links
        self.dereference = FormattedParameter("--dereference", False)
        # don't follow links in source
        self.no_dereference = FormattedParameter("--no-dereference", False)
        # preserve permissions, ownership, timestamps, extended attributes
        self.preserve = FormattedParameter("--preserve", False)
        # open files with O_DIRECT
        self.direct = FormattedParameter("--direct", False)
        # create sparse files when possible
        self.sparse = FormattedParameter("--sparse", False)
        # print progress every N seconds
        self.progress = FormattedParameter("--progress {}")
        # verbose output
        self.verbose = FormattedParameter("--verbose", False)
        # quiet output
        self.quiet = FormattedParameter("--quiet", False)
        # print help/usage
        self.print_usage = FormattedParameter("--help", False)
        # source path
        self.src_path = BasicParameter(None)
        # destination path
        self.dst_path = BasicParameter(None)

    def get_param_names(self):
        """Overriding the original get_param_names."""

        param_names = super(DcpCommand, self).get_param_names()

        # move key=dst_path to the end
        param_names.sort(key='dst_path'.__eq__)

        return param_names

    def set_dcp_params(self,
                       src_pool=None, src_cont=None, src_path=None,
                       dst_pool=None, dst_cont=None, dst_path=None,
                       prefix=None, display=True):
        """Set all common dcp params.

        Args:
            src_pool (str, optional): source pool uuid
            src_cont (str, optional): source container uuid
            src_path (str, optional): source path
            dst_pool (str, optional): destination pool uuid
            dst_cont (str, optional): destination container uuid
            dst_path (str, optional): destination path
            prefix (str, optional): prefix for uns path
            display (bool, optional): print updated params. Defaults to True.

        """
        if src_pool:
            self.daos_src_pool.update(src_pool,
                                      "daos_src_pool" if display else None)
        if src_cont:
            self.daos_src_cont.update(src_cont,
                                      "daos_src_cont" if display else None)
        if src_path:
            self.src_path.update(src_path,
                                 "src_path" if display else None)
        if dst_pool:
            self.daos_dst_pool.update(dst_pool,
                                      "daos_dst_pool" if display else None)
        if dst_cont:
            self.daos_dst_cont.update(dst_cont,
                                      "daos_dst_cont" if display else None)
        if dst_path:
            self.dst_path.update(dst_path,
                                 "dst_path" if display else None)
        if prefix:
            self.daos_prefix.update(prefix,
                                    "daos_prefix" if display else None) 

class Dcp(DcpCommand):
    """Class defining an object of type DcpCommand."""

    def __init__(self, hosts, timeout=30):
        """Create a dcp object."""
        super(Dcp, self).__init__(
            "/run/dcp/*", "dcp")

        # set params
        self.timeout = timeout
        self.hosts = hosts

    def run(self, tmp, processes):
        # pylint: disable=arguments-differ
        """Run the dcp command.

        Args:
            tmp (str): path for hostfiles
            processes: Number of processes for dcp command

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: In case dcp run command fails

        """
        self.log.info('Starting dcp')

        # Get job manager cmd
        mpirun = Mpirun(self, mpitype="mpich")
        mpirun.assign_hosts(self.hosts, tmp)
        mpirun.assign_processes(processes)
        mpirun.exit_status_exception = self.exit_status_exception

        # run dcp
        out = mpirun.run()

        return out

class DsyncCommand(ExecutableCommand):
    """Defines an object representing a dsync command."""

    def __init__(self, namespace, command):
        """Create a dsync Command object."""
        super(DsyncCommand, self).__init__(namespace, command)

        # dsync options

        # show differences, but do not synchronize files
        self.dryrun = FormattedParameter("--dryrun", False)
        # batch files into groups of N during copy
        self.batch_files = FormattedParameter("--batch-files {}")
        # IO buffer size in bytes (default 64MB)
        self.blocksize = FormattedParameter("--blocksize {}")
        # work size per task in bytes (default 64MB)
        self.chunksize = FormattedParameter("--chunksize {}")
        # DAOS source pool
        self.daos_src_pool = FormattedParameter("--daos-src-pool {}")
        # DAOS destination pool
        self.daos_dst_pool = FormattedParameter("--daos-dst-pool {}")
        # DAOS source container
        self.daos_src_cont = FormattedParameter("--daos-src-cont {}")
        # DAOS destination container
        self.daos_dst_cont = FormattedParameter("--daos-dst-cont {}")
        # DAOS prefix for unified namespace path
        self.daos_prefix = FormattedParameter("--daos-prefix {}")
        # read and compare file contents rather than compare size and mtime
        self.contents = FormattedParameter("--contents", False)
        # delete extraneous files from target
        self.delete = FormattedParameter("--delete", False)
        # copy original files instead of links
        self.dereference = FormattedParameter("--dereference", False)
        # don't follow links in source
        self.no_dereference = FormattedParameter("--no-dereference", False)
        # open files with O_DIRECT
        self.direct = FormattedParameter("--direct", False)
        # hardlink to files in DIR when unchanged
        self.link_dest = FormattedParameter("--link-dest {}")
        # create sparse files when possible
        self.sparse = FormattedParameter("--sparse", False)
        # print progress every N seconds
        self.progress = FormattedParameter("--progress {}")
        # verbose output
        self.verbose = FormattedParameter("--verbose", False)
        # quiet output
        self.quiet = FormattedParameter("--quiet", False)
        # print help/usage
        self.print_usage = FormattedParameter("--help", False)
        # source path
        self.src_path = BasicParameter(None)
        # destination path
        self.dst_path = BasicParameter(None)

    def get_param_names(self):
        """Overriding the original get_param_names."""

        param_names = super(DsyncCommand, self).get_param_names()

        # move key=dst_path to the end
        param_names.sort(key='dst_path'.__eq__)

        return param_names

    def set_dsync_params(self,
                         src_pool=None, src_cont=None, src_path=None,
                         dst_pool=None, dst_cont=None, dst_path=None,
                         prefix=None, display=True):
        """Set all common dsync params.
        Args:
            src_pool (str, optional): source pool uuid
            src_cont (str, optional): source container uuid
            src_path (str, optional): source path
            dst_pool (str, optional): destination pool uuid
            dst_cont (str, optional): destination container uuid
            dst_path (str, optional): destination path
            prefix (str, optional): prefix for uns path
            display (bool, optional): print updated params. Defaults to True.
        """
        if src_pool:
            self.daos_src_pool.update(src_pool,
                                      "daos_src_pool" if display else None)
        if src_cont:
            self.daos_src_cont.update(src_cont,
                                      "daos_src_cont" if display else None)
        if src_path:
            self.src_path.update(src_path,
                                 "src_path" if display else None)
        if dst_pool:
            self.daos_dst_pool.update(dst_pool,
                                      "daos_dst_pool" if display else None)
        if dst_cont:
            self.daos_dst_cont.update(dst_cont,
                                      "daos_dst_cont" if display else None)
        if dst_path:
            self.dst_path.update(dst_path,
                                 "dst_path" if display else None)
        if prefix:
            self.daos_prefix.update(prefix,
                                    "daos_prefix" if display else None) 

class Dsync(DsyncCommand):
    """Class defining an object of type DsyncCommand."""

    def __init__(self, hosts, timeout=30):
        """Create a dsync object."""
        super(Dsync, self).__init__(
            "/run/dsync/*", "dsync")

        # set params
        self.timeout = timeout
        self.hosts = hosts

    def run(self, tmp, processes):
        # pylint: disable=arguments-differ
        """Run the dsync command.
        Args:
            tmp (str): path for hostfiles
            processes: Number of processes for dsync command
        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.
        Raises:
            CommandFailure: In case dsync run command fails
        """
        self.log.info('Starting dsync')

        # Get job manager cmd
        mpirun = Mpirun(self, mpitype="mpich")
        mpirun.assign_hosts(self.hosts, tmp)
        mpirun.assign_processes(processes)
        mpirun.exit_status_exception = self.exit_status_exception

        # run dsync
        out = mpirun.run()

        return out
