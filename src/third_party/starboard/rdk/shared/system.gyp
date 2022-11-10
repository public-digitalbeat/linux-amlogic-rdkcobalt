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
# Copyright 2016 The Cobalt Authors. All rights reserved.
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
{
  'variables': {
    'pkg-config': 'pkg-config',
    'gst-packages': [
      'gstreamer-1.0',
      'gstreamer-app-1.0',
      'gstreamer-base-1.0',
      'gstreamer-video-1.0',
      'gstreamer-audio-1.0',
      'gstreamer-pbutils-1.0',
      'glib-2.0',
      'gobject-2.0'
    ],
    'wpeframework-packages': [
      'WPEFrameworkCore',
      'WPEFrameworkDefinitions',
      'WPEFrameworkPlugins',
    ],
    'has_securityagent%' : '<!(pkg-config securityagent && echo 1 || echo 0)',
    'has_cryptography%'  : '<!(make -C <(DEPTH)/third_party/starboard/rdk/shared/config.tests/cryptography >/dev/null 2>&1 && echo 1 || echo 0)'
  },
  'targets': [
    {
      'target_name': 'essos',
      'type': 'none',
      'link_settings': {
        'libraries': [
          '-lessos',
        ],
      },
    }, # essos
    {
      'target_name': 'gstreamer',
      'type': 'none',
      'direct_dependent_settings': {
        'cflags': [
          '<!@(<(pkg-config) --cflags <(gst-packages))',
        ],
      },
      'link_settings': {
        'ldflags': [
          '<!@(<(pkg-config) --libs-only-L --libs-only-other <(gst-packages))',
          '-lgstsecmemallocator',
          '-lgstamlsvpext',
        ],
        'libraries': [
          '<!@(<(pkg-config) --libs-only-l <(gst-packages))',
        ],
      },
    }, # gstreamer
    {
      'target_name': 'wpeframework',
      'type': 'none',
      'direct_dependent_settings': {
        'cflags': [
          '<!@(<(pkg-config) --cflags <(wpeframework-packages))',
        ],
      },
      'link_settings': {
        'ldflags': [
          '<!@(<(pkg-config) --libs-only-L --libs-only-other <(wpeframework-packages))',
        ],
        'libraries': [
          '<!@(<(pkg-config) --libs-only-l <(wpeframework-packages))',
        ],
      },
    }, # wpeframework
    {
      'target_name': 'rfcapi',
      'type': 'none',
      'direct_dependent_settings': {
        'cflags': [
          '-I=/usr/include/wdmp-c',
        ],
      },
      'link_settings': {
        'libraries': [
          '-lrfcapi',
        ],
      },
    }, # rfcapi
  ],
  'conditions': [
    ['<(has_ocdm)==1', {
     'targets': [
        {
          'target_name': 'ocdm',
          'type': 'none',
          'direct_dependent_settings': {
            'cflags': [
              '<!@(<(pkg-config) --cflags ocdm)',
            ],
          },
          'link_settings': {
            'ldflags': [
              '<!@(<(pkg-config) --libs-only-L --libs-only-other ocdm)',
            ],
            'libraries': [
              '<!@(<(pkg-config) --libs-only-l ocdm)',
              '-ldl',
            ],
          },
        }, # ocdm
      ],
    }],
    ['<(has_securityagent)==1', {
     'targets': [
        {
          'target_name': 'securityagent',
          'type': 'none',
          'direct_dependent_settings': {
            'cflags': [
              '<!@(<(pkg-config) --cflags securityagent)',
              '-DHAS_SECURITY_AGENT=1',
            ],
          },
          'link_settings': {
            'ldflags': [
              #'<!@(<(pkg-config) --libs-only-L --libs-only-other securityagent)',
            ],
            'libraries': [
              #'<!@(<(pkg-config) --libs-only-l securityagent)',
              '-Wl,--whole-archive',
              '-lsecurityagent',
              '-Wl,--no-whole-archive',
            ],
          },
        }, # securityagent
      ],
    }, {
     'targets': [
        {
          'target_name': 'securityagent',
          'type': 'none',
        }, # securityagent
      ],
    }],
    ['<(has_cryptography)==1', {
     'targets': [
        {
          'target_name': 'cryptography',
          'type': 'none',
          'direct_dependent_settings': {
            'cflags': [
              '<!@(<(pkg-config) --cflags WPEFrameworkCryptography) -DHAS_CRYPTOGRAPHY=1',
            ],
          },
          'link_settings': {
            'ldflags': [
              '<!@(<(pkg-config) --libs-only-L --libs-only-other WPEFrameworkCryptography)',
            ],
            'libraries': [
              '<!@(<(pkg-config) --libs-only-l WPEFrameworkCryptography)',
            ],
          },
        }, # cryptography
      ],
    }, {
     'targets': [
        {
          'target_name': 'cryptography',
          'type': 'none',
        }, # cryptography
      ],
    }]
  ],
}
