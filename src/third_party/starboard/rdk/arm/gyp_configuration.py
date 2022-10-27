# Copyright 2020 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright 2017 The Cobalt Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Starboard RDK Linux platform configuration."""

import os

from starboard.build import clang as clang_specification
from starboard.build import platform_configuration
from starboard.tools import build
from starboard.tools.testing import test_filter
from starboard.tools.toolchain import ar
from starboard.tools.toolchain import bash
from starboard.tools.toolchain import clang
from starboard.tools.toolchain import clangxx
from starboard.tools.toolchain import cp
from starboard.tools.toolchain import touch

class RDKPlatformConfig(platform_configuration.PlatformConfiguration):
  """Starboard RDK Linux platform configuration."""

  def __init__(self, platform):
    super(RDKPlatformConfig, self).__init__(platform)

    self.has_ocdm = os.environ.get('COBALT_HAS_OCDM', '0')
    self.sabi_json_path = 'starboard/sabi/arm/%s/sabi-v13.json' % (os.environ.get('COBALT_ARM_CALLCONVENTION', 'hardfp'))
    self.sysroot = os.path.realpath(os.environ.get('PKG_CONFIG_SYSROOT_DIR', '/'))
    self.AppendApplicationConfigurationPath(os.path.dirname(__file__))
    self.AppendApplicationConfigurationPath(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "shared")))
    self.rdk_video_resolution = os.environ.get('COBALT_VIDEO_RESOLUTION', '1080p')

  def GetBuildFormat(self):
    """Returns the desired build format."""
    # The comma means that ninja and qtcreator_ninja will be chained and use the
    # same input information so that .gyp files will only have to be parsed
    # once.
    return 'ninja,qtcreator_ninja'

  def GetVariables(self, configuration):
    variables = super(RDKPlatformConfig, self).GetVariables(configuration)
    variables.update({
        'clang': 0,
        'sysroot': self.sysroot,
        'has_ocdm': self.has_ocdm,
        'rdk_video_resolution': self.rdk_video_resolution,
    })
    variables.update({
        'cobalt_font_package': 'limited',
        'javascript_engine': 'v8',
    })
    return variables

  def GetEnvironmentVariables(self):
    if not hasattr(self, 'host_compiler_environment'):
      self.host_compiler_environment = build.GetHostCompilerEnvironment(
          clang_specification.GetClangSpecification(), self.build_accelerator)

    env_variables = {}
    env_variables.update({
        'CC': os.environ['CC'],
        'CXX': os.environ['CXX'],
        'LD': os.environ['CXX'],
        'CC_host': 'gcc -m32',
        'CXX_host': 'g++ -m32',
    })
    return env_variables

  def GetTargetToolchain(self, **kwargs):
    environment_variables = self.GetEnvironmentVariables()
    cc_path = environment_variables['CC']
    cxx_path = environment_variables['CXX']

    return [
        clang.CCompiler(path=cc_path),
        clang.CxxCompiler(path=cxx_path),
        clang.AssemblerWithCPreprocessor(path=cc_path),
        ar.StaticThinLinker(),
        ar.StaticLinker(),
        clangxx.ExecutableLinker(path=cxx_path, write_group=True),
        clangxx.SharedLibraryLinker(path=cxx_path),
        cp.Copy(),
        touch.Stamp(),
        bash.Shell(),
    ]

  def GetHostToolchain(self, **kwargs):
    environment_variables = self.GetEnvironmentVariables()
    cc_path = environment_variables['CC_host']
    cxx_path = environment_variables['CXX_host']

    return [
        clang.CCompiler(path=cc_path),
        clang.CxxCompiler(path=cxx_path),
        clang.AssemblerWithCPreprocessor(path=cc_path),
        ar.StaticThinLinker(),
        ar.StaticLinker(),
        clangxx.ExecutableLinker(path=cxx_path, write_group=True),
        clangxx.SharedLibraryLinker(path=cxx_path),
        cp.Copy(),
        touch.Stamp(),
        bash.Shell(),
    ]

  def GetLauncherPath(self):
    """Gets the path to the launcher module for this platform."""
    return os.path.dirname(__file__)

  def GetGeneratorVariables(self, config_name):
    del config_name
    generator_variables = {
        'qtcreator_session_name_prefix': 'cobalt',
    }
    return generator_variables

  def GetPathToSabiJsonFile(self):
    return self.sabi_json_path

def CreatePlatformConfig():
  return RDKPlatformConfig('rdk-arm')
