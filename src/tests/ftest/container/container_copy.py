#!/usr/bin/python               
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from apricot import TestWithServers

class containerCopy(TestWithServers):
    """Tests daos uitlity container copy functionality with ior and mdtest
    Args:
        TestWithServers ([type]): [description]
    """
    print("hello world")

    def test_container_copy(self):
        # Create a pool
        self.log.info("Create a pool")
        self.prepare_pool()

        # Check that the pool was created
        self.assertTrue(self.pool.check_files(self.hostlist_servers),
                        "Pool data was not created")

        # Create a container
        self.container = TestContainer(self.pool)
        self.container.get_params(self)
        self.container.create()