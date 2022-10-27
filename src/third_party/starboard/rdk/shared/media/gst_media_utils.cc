//
// Copyright 2020 Comcast Cable Communications Management, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <type_traits>

#include <glib.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "starboard/configuration.h"
#include "starboard/configuration_constants.h"
#include "starboard/common/log.h"
#include "third_party/starboard/rdk/shared/media/gst_media_utils.h"
#include "third_party/starboard/rdk/shared/log_override.h"

namespace third_party {
namespace starboard {
namespace rdk {
namespace shared {
namespace media {
namespace {

struct FeatureListDeleter {
  void operator()(GList* p) { gst_plugin_feature_list_free(p); }
};

struct CapsDeleter {
  void operator()(GstCaps* p) { gst_caps_unref(p); }
};

using UniqueFeatureList = std::unique_ptr<GList, FeatureListDeleter>;
using UniqueCaps = std::unique_ptr<GstCaps, CapsDeleter>;

UniqueFeatureList GetFactoryForCaps(GList* elements,
                                    UniqueCaps&& caps,
                                    GstPadDirection direction) {
  //SB_DLOG(INFO) << __FUNCTION__ << ": " << gst_caps_to_string(caps.get());
  SB_DCHECK(direction != GST_PAD_UNKNOWN);
  UniqueFeatureList candidates{
      gst_element_factory_list_filter(elements, caps.get(), direction, false)};
  return candidates;
}

template <typename C>
bool GstRegistryHasElementForCodec(C codec) {
  static_assert(std::is_same<C, SbMediaVideoCodec>::value ||
                std::is_same<C, SbMediaAudioCodec>::value, "Invalid codec");
  auto type = std::is_same<C, SbMediaVideoCodec>::value
                  ? GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO
                  : GST_ELEMENT_FACTORY_TYPE_MEDIA_AUDIO;
  static UniqueFeatureList parser_factories{gst_element_factory_list_get_elements(
      GST_ELEMENT_FACTORY_TYPE_PARSER | type, GST_RANK_MARGINAL)};
  static UniqueFeatureList decoder_factories{gst_element_factory_list_get_elements(
      GST_ELEMENT_FACTORY_TYPE_DECODER | type, GST_RANK_MARGINAL)};
  // UniqueFeatureList demuxer_factories{gst_element_factory_list_get_elements(
  //     GST_ELEMENT_FACTORY_TYPE_DEMUXER, GST_RANK_MARGINAL)};

  UniqueFeatureList elements;
  std::vector<std::string> caps;

  caps = CodecToGstCaps(codec);
  if (caps.empty()) {
    SB_DLOG(INFO) << "No caps for codec " << codec;
    return false;
  }

  for (auto single_caps : caps) {
    UniqueCaps gst_caps{gst_caps_from_string(single_caps.c_str())};
    elements = std::move(GetFactoryForCaps(decoder_factories.get(),
                                           std::move(gst_caps), GST_PAD_SINK));
    if (elements) {
      //SB_DLOG(INFO) << "Found decoder for " << single_caps;
      break;
    }
  }

  if (elements) {
    // Decoder is there.
    return true;
  }

  SB_DLOG(INFO) << "No decoder for codec " << codec << ". Falling back to parsers.";
  // No decoder. Check if there's a parser and a decoder accepting its caps.
  for (auto single_caps : caps) {
    UniqueCaps gst_caps{gst_caps_from_string(single_caps.c_str())};
    elements = std::move(GetFactoryForCaps(parser_factories.get(),
                                           std::move(gst_caps), GST_PAD_SINK));
    if (elements) {
      for (GList* iter = elements.get(); iter; iter = iter->next) {
        GstElementFactory* gst_element_factory =
            static_cast<GstElementFactory*>(iter->data);
        const GList* pad_templates =
            gst_element_factory_get_static_pad_templates(gst_element_factory);
        for (const GList* pad_templates_iter = pad_templates;
             pad_templates_iter;
             pad_templates_iter = pad_templates_iter->next) {
          GstStaticPadTemplate* pad_template =
              static_cast<GstStaticPadTemplate*>(pad_templates_iter->data);
          if (pad_template->direction == GST_PAD_SRC) {
            UniqueCaps pad_caps{gst_static_pad_template_get_caps(pad_template)};
            if (GetFactoryForCaps(decoder_factories.get(), std::move(pad_caps),
                                  GST_PAD_SINK)) {
              SB_DLOG(INFO) << "Found parser for " << single_caps
                            << " and decoder"
                               " accepting parser's src caps.";
              return true;
            }
          }
        }
      }
    }
  }

  SB_LOG(WARNING) << "Can not play codec " << codec;
  return false;
}

}  // namespace

bool GstRegistryHasElementForMediaType(SbMediaVideoCodec codec) {
  if (kSbMediaVideoCodecVp9 == codec && !kSbHasMediaWebmVp9Support)
    return false;
  return GstRegistryHasElementForCodec(codec);
}

bool GstRegistryHasElementForMediaType(SbMediaAudioCodec codec) {
  return GstRegistryHasElementForCodec(codec);
}

std::vector<std::string> CodecToGstCaps(SbMediaVideoCodec codec) {
  switch (codec) {
    default:
    case kSbMediaVideoCodecNone:
      return {};

    case kSbMediaVideoCodecH264:
      return {{"video/x-h264, stream-format=byte-stream, alignment=nal"}};

    case kSbMediaVideoCodecH265:
      return {{"video/x-h265"}};

    case kSbMediaVideoCodecMpeg2:
      return {{"video/mpeg, mpegversion=(int) 2"}};

    case kSbMediaVideoCodecTheora:
      return {{"video/x-theora"}};

    case kSbMediaVideoCodecVc1:
      return {{"video/x-vc1"}};

    case kSbMediaVideoCodecAv1:
      return {{"video/x-av1"}};

    case kSbMediaVideoCodecVp8:
      return {{"video/x-vp8"}};

    case kSbMediaVideoCodecVp9:
      return {{"video/x-vp9"}};
  }
}

std::vector<std::string> CodecToGstCaps(SbMediaAudioCodec codec,
                                        const SbMediaAudioSampleInfo* info) {
  switch (codec) {
    default:
    case kSbMediaAudioCodecNone:
      return {};

    case kSbMediaAudioCodecAac: {
      std::string primary_caps = "audio/mpeg, mpegversion=4";
      if (info) {
        primary_caps +=
            ", channels=" + std::to_string(info->number_of_channels);
        primary_caps += ", rate=" + std::to_string(info->samples_per_second);
        SB_LOG(INFO) << "Adding audio caps data from sample info.";
      }
      return {{primary_caps}, {"audio/aac"}};
    }

    case kSbMediaAudioCodecAc3:
    case kSbMediaAudioCodecEac3:
      return {{"audio/x-eac3"}};

    case kSbMediaAudioCodecOpus: {
      std::string primary_caps = "audio/x-opus, channel-mapping-family=0";
      if (info && info->audio_specific_config_size >= 19) {
        uint16_t codec_priv_size = info->audio_specific_config_size;
        const void* codec_priv = info->audio_specific_config;

        GstBuffer *tmp = gst_buffer_new_wrapped (g_memdup (codec_priv, codec_priv_size), codec_priv_size);
        GstCaps* gst_caps = gst_codec_utils_opus_create_caps_from_header (tmp, NULL);
        gchar* caps_str = gst_caps_to_string (gst_caps);

        primary_caps = caps_str;

        g_free (caps_str);
        gst_caps_unref (gst_caps);
        gst_buffer_unref (tmp);
      }
      return {{primary_caps}};
    }

    case kSbMediaAudioCodecVorbis:
      return {{"audio/x-vorbis"}};
  }
}

}  // namespace media
}  // namespace shared
}  // namespace rdk
}  // namespace starboard
}  // namespace third_party
