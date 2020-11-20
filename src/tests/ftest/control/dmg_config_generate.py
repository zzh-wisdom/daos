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

from apricot import TestWithServers
from avocado import fail_on
from server_utils import ServerFailed
from general_utils import pcmd


class ConfigGenerate(TestWithServers):
    """Test Class Description:

    Verify the veracity of the configuration created by the commmand and what
    the user specified, input verification and correct execution of the server
    once the generated configuration is propagated to the servers.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ConfigGenerate object."""
        super(ConfigGenerate, self).__init__(*args, **kwargs)
        self.setup_start_servers = False

    @fail_on(ServerFailed)
    def dmg_generate_config(self):
        """ Verify that dmg can generate an accurate configuration file."""

        # Let's create an empty config file on the server/s
        cfg_file = self.get_config_file("daos_server", "server_discover")
        pcmd(self.hostlist_servers, "touch {}".format(cfg_file))

        # Setup the server managers
        self.add_server_manager()
        self.configure_manager(
            "server", self.server_managers[0], self.hostlist_servers,
            self.hostfile_servers_slots)

        # Update the config value for the server to an empty config file.
        # Then, start the server in discovery mode
        self.server_managers[0].manager.job.config.update(
            cfg_file, "daos_server.config")
        try:
            self.server_managers[0].detect_start_mode("discover")
        except ServerFailed as err:
            self.fail("Error starting server in discovery mode: {}".format(err))

        # We need to scan storage and net to check what's on the config file
        storage_info = self.get_dmg_command().storage_scan(verbose=True)
        network_info = self.dmg_dmg_command().network_scan()


        # Let's get the config file contents
        yaml_data = self.get_dmg_command().config_generate()

        # Verify yaml_data contents

        # Propagate file to other servers


        # Verify that all daos_io_server instances are started.
