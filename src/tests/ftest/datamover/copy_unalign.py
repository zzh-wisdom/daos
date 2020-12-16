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
from os.path import join
from resource import getpagesize

class CopyUnalignTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for Datamover unaligned data.
    Test Class Description:
        Tests the datamover copy utility with unaligned data.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CopyUnalignTest object."""
        super(CopyUnalignTest, self).__init__(*args, **kwargs)

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyUnalignTest, self).setUp()

        # Get the parameters
        self.test_file_list = self.params.get(
            "test_file_list", "/run/ior/*")
        self.ior_flags = self.params.get(
            "ior_flags", "/run/ior/*")
        self.flags_write = self.ior_flags[0]
        self.flags_read = self.ior_flags[1]

    def test_copy_unalign(self):
        """
        Test Description:
            DAOS-6182: Verify copying non-aligned data
        Use Cases:
            Create pool.
            Create POSIX container1 and container2 in pool.
            Using variying unaligned file sizes:
                Create a file in container1.
                Copy the file from container1 to container2.
                Copy the file from container1 to external POSIX.
                Create a file in external POSIX.
                Copy the file from external POSIX to container2.
        :avocado: tags=all,datamover,full_regression
        :avocado: tags=copy_unalign
        """
        # Create pool and containers
        pool1 = self.create_pool()
        container1 = self.create_cont(pool1)
        container2 = self.create_cont(pool1)

        # Get some POSIX test directories
        posix_test_path1 = self.new_posix_dir()
        posix_test_path2 = self.new_posix_dir()

        # Use the pagesize as a reference for file sizes
        # This is just an approximation, since the remote
        # system may have a different pagesize
        pagesize = getpagesize()

        # Run with each test file
        for (test_file, size_expr) in self.test_file_list:
            # Plug the pagesize into the expression and evaluate it
            block_size = eval(size_expr.format(pagesize))
            test_desc_prefix = "copy_unalign ({})".format(test_file)
            self.ior_cmd.block_size.update(block_size)
            self.ior_cmd.transfer_size.update(block_size)

            # Get paths to the source and destination
            src_daos_file = "/" + test_file
            dst_daos_file = "/" + test_file
            src_posix_file = join(posix_test_path1, test_file)
            dst_posix_file = join(posix_test_path2, test_file)

            # Create the source test files
            self.set_ior_location_and_run("DAOS", src_daos_file,
                                          pool1, container1,
                                          flags=self.flags_write)
            self.set_ior_location_and_run("POSIX", src_posix_file,
                                          flags=self.flags_write)

            # DAOS -> DAOS
            self.set_src_location("DAOS", src_daos_file, pool1, container1)
            self.set_dst_location("DAOS", dst_daos_file, pool1, container2)
            test_desc = test_desc_prefix + " (DAOS->DAOS)"
            self.run_datamover(test_desc=test_desc)
            self.set_ior_location_and_run("DAOS", dst_daos_file,
                                          pool1, container2,
                                          flags=self.flags_read)

            # DAOS -> POSIX
            self.set_src_location("DAOS", src_daos_file, pool1, container1)
            self.set_dst_location("POSIX", dst_posix_file)
            test_desc = test_desc_prefix + " (DAOS->POSIX)"
            self.run_datamover(test_desc=test_desc)
            self.set_ior_location_and_run("POSIX", dst_posix_file,
                                          flags=self.flags_read)

            # POSIX -> DAOS
            self.set_src_location("POSIX", src_posix_file)
            self.set_dst_location("DAOS", dst_daos_file, pool1, container2)
            test_desc = test_desc_prefix + " (POSIX->DAOS)"
            self.run_datamover(test_desc=test_desc)
            self.set_ior_location_and_run("DAOS", dst_daos_file,
                                          pool1, container2,
                                          flags=self.flags_read)
