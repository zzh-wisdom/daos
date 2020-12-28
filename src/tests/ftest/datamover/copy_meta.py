#!/usr/bin/python
'''
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
'''
from data_mover_test_base import DataMoverTestBase
from os.path import basename, join


class CopyMetaTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for POSIX DataMover metadata validation.

    Test Class Description:
        Tests metadata preservation of the POSIX DataMover utilities.
    :avocado: recursive
    """

    def test_copy_meta_dcp(self):
        """
        Test Description:
            Verifies that POSIX metadata is preserved.
            Uses the dcp tool.
            DAOS-6390: Verify POSIX metadata.
        :avocado: tags=all,daily_regression
        :avocado: tags=datamover,dcp
        :avocado: tags=copy_meta,copy_meta_dcp
        """
        self._copy_meta("DCP")

    def _copy_meta(self, tool):
        """
        Use Cases:
            Create pool1.
            Create cont1 and cont2 in pool1.
            Create a source directory in cont1 that contains:
                1 directory, 1 file, 1 symlink.
                xattrs on the directory and file.
            Create a similar source directory in an external POSIX file system.
            Copy the DAOS source to another DAOS directory.
            Copy the DAOS source to an external POSIX file system.
            Copy the POSIX source to another DAOS directory.
            For each case, verify that permissions and owners are preserved.
            Repeat each case, but with the --preserve flag.
            For each case, verify that xattrs and timestamps are preserved.
        """
        test_desc = "_copy_meta('{}')".format(tool)

        # Set the tool to use
        self.set_tool(tool)

        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.dfuse_hosts)

        # Create 1 pool
        pool1 = self.create_pool()

        # Create 1 source container with test data
        cont1 = self.create_cont(pool1)
        daos_src_path = self.new_daos_test_path(False)
        dfuse_src_path = "{}/{}/{}{}".format(
            self.dfuse.mount_dir.value, pool1.uuid, cont1.uuid, daos_src_path)
        self.create_data(dfuse_src_path)

        # Create 1 source posix path with test data
        posix_src_path = self.new_posix_test_path()
        self.create_data(posix_src_path)

        # Run each variation with and without the --preserve option
        # For each case, create a new destination directory.
        # For DAOS, cont1 is used as the source and destination.
        for do_preserve in [False, True]:
            #for do_preserve in [False]:
            preserve_desc = " (preserve={})".format(str(do_preserve))
            # DAOS -> DAOS
            daos_dst_path = self.new_daos_test_path(False)
            dfuse_dst_path = "{}/{}/{}{}".format(
                self.dfuse.mount_dir.value, pool1.uuid, cont1.uuid, daos_dst_path)
            self.set_datamover_params(
                "DAOS", daos_src_path, pool1, cont1,
                "DAOS", daos_dst_path, pool1, cont1)
            self.dcp_cmd.preserve.update(do_preserve, "preserve")
            self.run_datamover(
                test_desc + "(DAOS->DAOS)" + preserve_desc,
                set_params=False)
            self.compare_data(
                dfuse_src_path, dfuse_dst_path,
                cmp_times=do_preserve, cmp_xattr=do_preserve)

            # DAOS -> POSIX
            posix_dst_path = self.new_posix_test_path(False)
            self.set_datamover_params(
                "DAOS", daos_src_path, pool1, cont1,
                "POSIX", posix_dst_path)
            self.dcp_cmd.preserve.update(do_preserve, "preserve")
            self.run_datamover(
                test_desc + "(DAOS->POSIX)" + preserve_desc,
                set_params=False)
            self.compare_data(
                dfuse_src_path, posix_dst_path,
                cmp_times=do_preserve, cmp_xattr=do_preserve)

            # POSIX -> DAOS
            daos_dst_path = self.new_daos_test_path(False)
            dfuse_dst_path = "{}/{}/{}{}".format(
                self.dfuse.mount_dir.value, pool1.uuid, cont1.uuid, daos_dst_path)
            self.set_datamover_params(
                "POSIX", posix_src_path, None, None,
                "DAOS", daos_dst_path, pool1, cont1)
            self.dcp_cmd.preserve.update(do_preserve, "preserve")
            self.run_datamover(
                test_desc + "(POSIX->DAOS)" + preserve_desc,
                set_params=False)
            # The source directory is created IN the destination
            #dst_path = join(dst_path, basename(posix_src_path))
            self.compare_data(
                posix_src_path, dfuse_dst_path,
                cmp_times=do_preserve, cmp_xattr=do_preserve)

    def create_data(self, path):
        """Create the test data.

        Args:
            path (str): Where to create the data.
        """
        cmd_list = [
            # One directory
            "mkdir -p '{}'".format(join(path, "dir1")),
            "pushd '{}'".format(path),

            # xattrs for the directory
            "setfattr -n 'user.dir1_attr1' -v 'dir1_value1' 'dir1'",
            "setfattr -n 'user.dir1_attr2' -v 'dir1_value2' 'dir1'",

            # One file in the directory
            "echo 'test_data' > 'dir1/file1'",

            # xattrs for the file
            "setfattr -n 'user.file1_attr1' -v 'file1_value1' 'dir1/file1'",
            "setfattr -n 'user.file1_attr2' -v 'file1_value2' 'dir1/file1'",

            # One symlink in the directory
            "ln -s 'file1' 'dir1/link1'",

            "popd"
        ]
        self.execute_cmd_list(cmd_list);

    def compare_data(self, path1, path2, cmp_filetype=True,
                     cmp_perms=True, cmp_owner=True, cmp_times=False,
                     cmp_xattr=False):
        """Compare the test data.

        Args:
            path1 (str): The left-hand side to compare.
            path2 (str): The right-hand side to compare.
            cmp_filetype (bool, optional): Whether to compare the filetype.
                Default is True.
            cmp_perms (bool, optional): Whether to compare the permissions.
                Default is True.
            cmp_owner (bool, optional): Whether to compare the user and group
                ownership. Default is True.
            cmp_times (bool, optional): Whether to compare atime and mtime.
                Default is False.
            cmp_xattr (bool, optional): Whether to compare xattrs.
                Default is False.
        """
        self.log.info("compare_data('%s', '%s')", path1, path2)

        # Generate the fields to compare
        field_printf = ""
        if cmp_filetype:
            field_printf += "File Type: %F\\n"
        if cmp_perms:
            field_printf += "Permissions: %A\\n"
        if cmp_owner:
            field_printf += "Group Name: %G\\n"
            field_printf += "User Name: %U\\n"
        if cmp_times:
            field_printf += "atime: %X\\n"
            field_printf += "mtime: %Y\\n"

        # Use stat to get the perms, etc.
        if field_printf:
            stat_cmd = "stat --printf '{}'".format(field_printf)
        else:
            stat_cmd = None

        # Use getfattr to get the xattrs
        if cmp_xattr:
            xattr_cmd = "getfattr -d -h"
        else:
            xattr_cmd = None

        # Diff the fields for each entry
        for entry in ["dir1", "dir1/file1", "dir1/link1"]:
            entry1 = join(path1, entry)
            entry2 = join(path2, entry)
            if stat_cmd:
                diff_cmd = "diff"
                diff_cmd += " <({} '{}' 2>&1) <({} '{}' 2>&1)".format(
                    stat_cmd, entry1, stat_cmd, entry2)
                self.execute_cmd(diff_cmd)
            if xattr_cmd:
                diff_cmd = "diff -I '^#'"
                diff_cmd += " <({} '{}' 2>&1) <({} '{}' 2>&1)".format(
                    xattr_cmd, entry1, xattr_cmd, entry2)
                self.execute_cmd(diff_cmd)

    def execute_cmd_list(self, cmd_list):
        """Execute a list of commands, separated by &&.

        Args:
            cmd_list (list): A list of commands to execute.
        """
        cmd = " &&\n".join(cmd_list)
        self.execute_cmd(cmd)
