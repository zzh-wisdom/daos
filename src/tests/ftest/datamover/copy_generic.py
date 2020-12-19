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


class CopyGenericTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for DataMover generic container copy validation.

    Test Class Description:
        Tests basic functionality of the generic container copy utilities.
        Tests the following cases:
            TODO
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyGenericTest, self).setUp()

        # Get the parameters
        self.ior_flags = self.params.get(
            "ior_flags", "/run/ior/*")
        self.test_file = self.ior_cmd.test_file.value

    def test_copy_generic_dcp(self):
        """
        Test Description:
            TODO
        :avocado: tags=all,pr,datamover,dcp
        :avocado: tags=copy_generic,copy_generic_dcp
        """
        self._copy_generic("DCP")

    def test_copy_generic_cont_copy(self):
        """
        Test Description:
            TODO
        :avocado: tags=all,pr,datamover,daos_cont_copy
        :avocado: tags=copy_generic,copy_generic_cont_copy
        """
        self.fail("Not implemented")
        # TODO
        #self._copy_generic("CONT_COPY")

    def _copy_generic(self, tool):
        """
        Use Cases:
            TODO
        """
        # Set the tool to use
        self.set_tool(tool)

        # test params
        mdtest_flags = self.params.get("mdtest_flags", "/run/mdtest/*")
        file_size = self.params.get("bytes", "/run/mdtest/*")
        # create pool and cont
        self.create_pool()
        self.create_cont(self.pool[0])

        # run mdtest to create data in cont1
        self.mdtest_cmd.write_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", "/", self.pool[0], self.container[0],
            flags=mdtest_flags[0])

        # create second container
        self.create_cont(self.pool[0])

        # copy data from cont1 to cont2
        self.run_datamover(
            "daoscont1_to_daoscont2 (cont1 to cont2)",
            "DAOS", None, self.pool[0], self.container[0],
            "DAOS", None, self.pool[0], self.container[1])

        # run mdtest read on cont2
        self.mdtest_cmd.read_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", "/", self.pool[0], self.container[1],
            flags=mdtest_flags[1])
