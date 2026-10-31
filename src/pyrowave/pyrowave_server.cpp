/**
 * @file pyrowave_server.cpp
 * @brief Server-side PyroWave integration for Apollo.
 */
#include "pyrowave_server.h"

#include <algorithm>
#include <limits>
#include <mutex>

#ifdef PYROWAVE_ENABLED
  #include <pyrowave/pyrowave.h>
#endif

#include "src/logging.h"

namespace pyrowave {

  bool
  is_experimental_video_format(std::uint32_t video_format) {
    return video_format == LI_PYROWAVE_VIDEO_FORMAT;
  }

  bool
  is_compiled() {
#ifdef PYROWAVE_ENABLED
    return true;
#else
    return false;
#endif
  }

  bool
  is_server_runtime_available() {
#ifdef PYROWAVE_ENABLED
    static std::once_flag once;
    static bool available = false;
    std::call_once(once, [] {
      pyrowave_device device = nullptr;
      if (pyrowave_create_default_device(&device) == PYROWAVE_SUCCESS && device) {
        pyrowave_device_destroy(device);
        available = true;
      } else {
        BOOST_LOG(warning) << "PyroWave runtime probe failed: no usable Vulkan device";
      }
    });
    return available;
#else
    return false;
#endif
  }

  int
  limit_rtp_packet_size(int requested_packet_size, std::uint32_t max_wire_packet_size) {
    constexpr std::uint32_t outer_header_overhead = 16;
    if (requested_packet_size <= static_cast<int>(outer_header_overhead) ||
        max_wire_packet_size > LI_PYROWAVE_MAX_PACKET_SIZE) {
      return 0;
    }
    const auto wire_size = std::min(
      static_cast<std::uint32_t>(requested_packet_size) - outer_header_overhead, max_wire_packet_size);
    if (wire_size <= 2 * LI_PYROWAVE_WIRE_HEADER_SIZE) {
      return 0;
    }
    return static_cast<int>(wire_size + outer_header_overhead);
  }

  negotiate_result_t
  negotiate_client_caps(std::uint16_t protocol_version, std::uint16_t bitstream_version,
                        std::uint16_t payload_version, std::uint32_t capability_flags,
                        std::uint32_t max_packet_size, int dynamic_range, bool full_range) {
    const auto base_caps = dynamic_range == 1 ? LI_PYROWAVE_REQUIRED_HDR10_BASE_CAPABILITIES :
                           dynamic_range == 2 ? LI_PYROWAVE_REQUIRED_HLG_BASE_CAPABILITIES :
                                                LI_PYROWAVE_REQUIRED_SDR_BASE_CAPABILITIES;
    const auto range_cap = full_range ? LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE :
                                        LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE;

    LI_PYROWAVE_CAPABILITIES server_caps {
      .protocolVersion = LI_PYROWAVE_PROTOCOL_VERSION,
      .bitstreamVersion = LI_PYROWAVE_BITSTREAM_VERSION,
      .payloadVersion = LI_PYROWAVE_PAYLOAD_VERSION,
      .reserved = 0,
      .capabilityFlags = SERVER_CAPABILITY_FLAGS,
      .maxPacketSize = LI_PYROWAVE_MAX_PACKET_SIZE,
    };
    LI_PYROWAVE_CAPABILITIES client_caps {
      .protocolVersion = protocol_version,
      .bitstreamVersion = bitstream_version,
      .payloadVersion = payload_version,
      .reserved = 0,
      .capabilityFlags = capability_flags,
      .maxPacketSize = max_packet_size,
    };
    LI_PYROWAVE_CAPABILITIES negotiated {};
    const auto required = base_caps | range_cap;
    if (LiPyrowaveNegotiate(&server_caps, &client_caps, required, &negotiated) != LI_PYROWAVE_NEGOTIATION_OK) {
      return { false, 0 };
    }
    return { true, negotiated.maxPacketSize };
  }

  std::optional<std::vector<std::uint8_t>>
  packetize_frame(std::uint32_t frame_id, std::uint32_t rtp_timestamp,
                  const std::uint8_t *codec_bytes, std::size_t codec_size,
                  std::size_t packet_boundary) {
    const std::size_t header_size = LI_PYROWAVE_WIRE_HEADER_SIZE;
    if (!codec_bytes || codec_size == 0 || codec_size > std::numeric_limits<std::uint32_t>::max() ||
        packet_boundary <= header_size || packet_boundary > LI_PYROWAVE_MAX_PACKET_SIZE ||
        frame_id == 0) {
      return std::nullopt;
    }

    // No frame metadata is emitted for SDR sessions; the protected payload is
    // exactly the codec byte stream.
    const std::size_t metadata_length = 0;
    const std::size_t protected_size = metadata_length + codec_size;
    const std::size_t payload_boundary = packet_boundary - header_size;
    const std::size_t data_block_count =
      protected_size / payload_boundary + (protected_size % payload_boundary != 0);
    const std::size_t fec_group_count =
      (data_block_count + LI_PYROWAVE_FEC_DATA_PER_GROUP - 1) / LI_PYROWAVE_FEC_DATA_PER_GROUP;
    const std::size_t block_count = data_block_count + fec_group_count;

    if (data_block_count == 0 || data_block_count > LI_PYROWAVE_MAX_FRAME_BLOCKS ||
        block_count > LI_PYROWAVE_MAX_FRAME_BLOCKS ||
        block_count > std::numeric_limits<std::uint16_t>::max() ||
        payload_boundary > std::numeric_limits<std::uint16_t>::max()) {
      BOOST_LOG(warning) << "PyroWave packetizer: unusable frame geometry, protected_bytes="
                         << protected_size << ", payload_boundary=" << payload_boundary;
      return std::nullopt;
    }

    try {
      std::vector<std::uint8_t> out;
      out.reserve((block_count + 1) * packet_boundary);

      const std::uint16_t fec_block_payload_size = static_cast<std::uint16_t>(payload_boundary);
      const std::uint8_t fec_scheme = LI_PYROWAVE_FEC_SCHEME_XOR;

      // First pass: build FRAME_HEADER and all DATA packets into `out`,
      // remembering each packet's payload location for parity computation.
      struct span_t {
        std::size_t offset;
      };
      std::vector<span_t> data_payload_spans;
      data_payload_spans.reserve(data_block_count);

      auto build_packet = [&](const LI_PYROWAVE_PACKET_HEADER &header, const std::uint8_t *payload) -> bool {
        const auto packet_offset = out.size();
        out.resize(packet_offset + header.headerLength + header.payloadLength);
        std::size_t packet_size = 0;
        const auto result = LiPyrowaveBuildPacket(
          &header, payload, header.payloadLength,
          out.data() + packet_offset, out.size() - packet_offset, &packet_size);
        if (result != LI_PYROWAVE_PACKET_OK || packet_size == 0) {
          BOOST_LOG(warning) << "PyroWave packetizer: LiPyrowaveBuildPacket failed ("
                             << static_cast<int>(result) << ')';
          return false;
        }
        out.resize(packet_offset + packet_size);
        return true;
      };

      // FRAME_HEADER packet: body is a copy of the metadata prefix (none here),
      // zero padded to the block capacity.
      {
        std::vector<std::uint8_t> frame_header_payload(payload_boundary, 0);
        LI_PYROWAVE_PACKET_HEADER header {
          .version = LI_PYROWAVE_PROTOCOL_VERSION,
          .packetKind = LI_PYROWAVE_PACKET_FRAME_HEADER,
          .flags = LI_PYROWAVE_FLAG_START_OF_FRAME,
          .reserved = 0,
          .headerLength = static_cast<std::uint16_t>(header_size),
          .metadataFlags = 0,
          .frameId = frame_id,
          .rtpTimestamp = rtp_timestamp,
          .codecPayloadLength = static_cast<std::uint32_t>(codec_size),
          .protectedPayloadLength = static_cast<std::uint32_t>(protected_size),
          .metadataLength = 0,
          .fecScheme = fec_scheme,
          .reserved2 = 0,
          .dataBlockCount = static_cast<std::uint16_t>(data_block_count),
          .parityBlockCount = static_cast<std::uint16_t>(fec_group_count),
          .fecBlockPayloadSize = fec_block_payload_size,
          .reserved3 = 0,
          .blockIndex = 0,
          .blockCount = static_cast<std::uint16_t>(block_count),
          .fecGroupIndex = 0,
          .fecDataCount = 0,
          .fecParityCount = 0,
          .fecShardIndex = 0,
          .reserved4 = 0,
          .payloadLength = static_cast<std::uint16_t>(payload_boundary),
          .reserved5 = 0,
          .reserved6 = 0,
        };
        if (!build_packet(header, frame_header_payload.data())) {
          return std::nullopt;
        }
      }

      // DATA packets: byte split of the codec stream, zero padded to capacity.
      for (std::size_t index = 0; index < data_block_count; ++index) {
        const auto offset = index * payload_boundary;
        const auto source_size = std::min(payload_boundary, protected_size - offset);
        std::vector<std::uint8_t> block_payload(payload_boundary, 0);
        std::copy_n(codec_bytes + offset, source_size, block_payload.begin());
        data_payload_spans.push_back({ out.size() + header_size });

        const auto group_index = index / LI_PYROWAVE_FEC_DATA_PER_GROUP;
        const auto group_start = group_index * LI_PYROWAVE_FEC_DATA_PER_GROUP;
        const auto group_data_count = static_cast<std::uint8_t>(
          std::min<std::size_t>(LI_PYROWAVE_FEC_DATA_PER_GROUP, data_block_count - group_start));

        LI_PYROWAVE_PACKET_HEADER header {
          .version = LI_PYROWAVE_PROTOCOL_VERSION,
          .packetKind = LI_PYROWAVE_PACKET_DATA,
          .flags = static_cast<std::uint8_t>(
            (index == 0 ? LI_PYROWAVE_FLAG_START_OF_FRAME : 0) |
            (index + 1 == data_block_count ? LI_PYROWAVE_FLAG_END_OF_FRAME : 0)),
          .reserved = 0,
          .headerLength = static_cast<std::uint16_t>(header_size),
          .metadataFlags = 0,
          .frameId = frame_id,
          .rtpTimestamp = rtp_timestamp,
          .codecPayloadLength = static_cast<std::uint32_t>(codec_size),
          .protectedPayloadLength = static_cast<std::uint32_t>(protected_size),
          .metadataLength = 0,
          .fecScheme = fec_scheme,
          .reserved2 = 0,
          .dataBlockCount = static_cast<std::uint16_t>(data_block_count),
          .parityBlockCount = static_cast<std::uint16_t>(fec_group_count),
          .fecBlockPayloadSize = fec_block_payload_size,
          .reserved3 = 0,
          .blockIndex = static_cast<std::uint16_t>(index),
          .blockCount = static_cast<std::uint16_t>(block_count),
          .fecGroupIndex = static_cast<std::uint16_t>(group_index),
          .fecDataCount = group_data_count,
          .fecParityCount = LI_PYROWAVE_FEC_PARITY_SHARDS,
          .fecShardIndex = static_cast<std::uint8_t>(index % LI_PYROWAVE_FEC_DATA_PER_GROUP),
          .reserved4 = 0,
          .payloadLength = static_cast<std::uint16_t>(payload_boundary),
          .reserved5 = 0,
          .reserved6 = 0,
        };
        if (!build_packet(header, block_payload.data())) {
          return std::nullopt;
        }
      }

      // PARITY packets: byte-wise XOR of each group's DATA payloads. DATA
      // payloads are already zero padded and live inside `out`.
      for (std::size_t group = 0; group < fec_group_count; ++group) {
        const auto group_start = group * LI_PYROWAVE_FEC_DATA_PER_GROUP;
        const auto group_end = std::min(data_block_count, group_start + LI_PYROWAVE_FEC_DATA_PER_GROUP);
        const auto group_data_count = group_end - group_start;

        std::vector<std::uint8_t> parity(payload_boundary, 0);
        for (std::size_t index = group_start; index < group_end; ++index) {
          const auto *data = out.data() + data_payload_spans[index].offset;
          for (std::size_t byte = 0; byte < payload_boundary; ++byte) {
            parity[byte] ^= data[byte];
          }
        }

        LI_PYROWAVE_PACKET_HEADER header {
          .version = LI_PYROWAVE_PROTOCOL_VERSION,
          .packetKind = LI_PYROWAVE_PACKET_PARITY,
          .flags = LI_PYROWAVE_FLAG_FEC_PARITY,
          .reserved = 0,
          .headerLength = static_cast<std::uint16_t>(header_size),
          .metadataFlags = 0,
          .frameId = frame_id,
          .rtpTimestamp = rtp_timestamp,
          .codecPayloadLength = static_cast<std::uint32_t>(codec_size),
          .protectedPayloadLength = static_cast<std::uint32_t>(protected_size),
          .metadataLength = 0,
          .fecScheme = fec_scheme,
          .reserved2 = 0,
          .dataBlockCount = static_cast<std::uint16_t>(data_block_count),
          .parityBlockCount = static_cast<std::uint16_t>(fec_group_count),
          .fecBlockPayloadSize = fec_block_payload_size,
          .reserved3 = 0,
          .blockIndex = static_cast<std::uint16_t>(data_block_count + group),
          .blockCount = static_cast<std::uint16_t>(block_count),
          .fecGroupIndex = static_cast<std::uint16_t>(group),
          .fecDataCount = static_cast<std::uint8_t>(group_data_count),
          .fecParityCount = LI_PYROWAVE_FEC_PARITY_SHARDS,
          .fecShardIndex = static_cast<std::uint8_t>(group_data_count),
          .reserved4 = 0,
          .payloadLength = static_cast<std::uint16_t>(payload_boundary),
          .reserved5 = 0,
          .reserved6 = 0,
        };
        if (!build_packet(header, parity.data())) {
          return std::nullopt;
        }
      }

      return out;
    }
    catch (const std::bad_alloc &) {
      BOOST_LOG(warning) << "PyroWave packetizer: allocation failure";
      return std::nullopt;
    }
    catch (...) {
      BOOST_LOG(warning) << "PyroWave packetizer: unexpected exception";
      return std::nullopt;
    }
  }

#ifdef PYROWAVE_ENABLED
  encode_session_t::~encode_session_t() {
    destroy();
  }

  void
  encode_session_t::destroy() {
    if (encoder_) {
      pyrowave_encoder_destroy(reinterpret_cast<pyrowave_encoder>(encoder_));
      encoder_ = nullptr;
    }
    if (device_) {
      pyrowave_device_destroy(reinterpret_cast<pyrowave_device>(device_));
      device_ = nullptr;
    }
  }

  bool
  encode_session_t::init(int width, int height, bool full_range, int bitrate_kbps, int framerate) {
    destroy();

    if (width <= 0 || height <= 0 || width % 2 || height % 2 || framerate <= 0) {
      BOOST_LOG(error) << "PyroWave encoder: invalid session parameters";
      return false;
    }

    pyrowave_device raw_device = nullptr;
    if (pyrowave_create_default_device(&raw_device) != PYROWAVE_SUCCESS || !raw_device) {
      BOOST_LOG(error) << "PyroWave encoder: device creation failed";
      return false;
    }
    device_ = raw_device;

    pyrowave_encoder_create_info encoder_info {};
    encoder_info.device = raw_device;
    encoder_info.width = width;
    encoder_info.height = height;
    encoder_info.chroma = PYROWAVE_CHROMA_SUBSAMPLING_420;

    pyrowave_encoder raw_encoder = nullptr;
    if (pyrowave_encoder_create(&encoder_info, &raw_encoder) != PYROWAVE_SUCCESS || !raw_encoder) {
      BOOST_LOG(error) << "PyroWave encoder: encoder creation failed";
      destroy();
      return false;
    }
    encoder_ = raw_encoder;

    pyrowave_color_metadata color {};
    color.primaries = PYROWAVE_COLOR_PRIMARIES_BT709;
    color.transfer = PYROWAVE_TRANSFER_BT709;
    color.transform = PYROWAVE_YCBCR_BT709;
    color.range = full_range ? PYROWAVE_YCBCR_FULL : PYROWAVE_YCBCR_LIMITED;
    color.chroma_siting = 0;
    if (pyrowave_encoder_set_color_metadata(raw_encoder, &color) != PYROWAVE_SUCCESS) {
      BOOST_LOG(error) << "PyroWave encoder: setting color metadata failed";
      destroy();
      return false;
    }

    width_ = width;
    height_ = height;

    // Reserve a generous bitstream buffer; the encoder only writes up to the
    // per-frame rate control budget.
    bitstream_.resize(16 * 1024 * 1024);

    BOOST_LOG(info) << "PyroWave encoder initialized: " << width << "x" << height
                    << " @" << framerate << " fps, " << bitrate_kbps << " kbps, "
                    << (full_range ? "full" : "limited") << " range";
    return true;
  }

  std::optional<std::vector<std::uint8_t>>
  encode_session_t::encode(const std::uint8_t *y_plane, std::size_t y_stride,
                           const std::uint8_t *uv_plane, std::size_t uv_stride,
                           std::size_t max_bytes) {
    if (!encoder_) {
      return std::nullopt;
    }

    pyrowave_cpu_buffer buffer {};
    buffer.data[0] = const_cast<std::uint8_t *>(y_plane);
    buffer.data[1] = const_cast<std::uint8_t *>(uv_plane);
    buffer.data[2] = nullptr;
    buffer.row_stride_in_bytes[0] = y_stride;
    buffer.row_stride_in_bytes[1] = uv_stride;
    buffer.row_stride_in_bytes[2] = 0;
    buffer.plane_size_in_bytes[0] = y_stride * height_;
    buffer.plane_size_in_bytes[1] = uv_stride * (height_ / 2);
    buffer.plane_size_in_bytes[2] = 0;
    buffer.width = width_;
    buffer.height = height_;
    buffer.format = PYROWAVE_CPU_BUFFER_FORMAT_NV12;

    pyrowave_rate_control rate_control {};
    rate_control.maximum_bitstream_size = max_bytes;

    auto *encoder = reinterpret_cast<pyrowave_encoder>(encoder_);
    if (pyrowave_encoder_encode_cpu(encoder, &buffer, &rate_control) != PYROWAVE_SUCCESS) {
      BOOST_LOG(error) << "PyroWave encode failed";
      return std::nullopt;
    }

    size_t num_packets = 0;
    if (pyrowave_encoder_compute_num_packets(encoder, max_bytes, &num_packets) != PYROWAVE_SUCCESS ||
        num_packets == 0 || num_packets > 4096) {
      BOOST_LOG(error) << "PyroWave packet count is unusable: " << num_packets;
      return std::nullopt;
    }

    packets_.resize(num_packets);
    if (pyrowave_encoder_packetize(encoder, packets_.data(), max_bytes, &num_packets,
                                   bitstream_.data(), bitstream_.size()) != PYROWAVE_SUCCESS ||
        num_packets == 0) {
      BOOST_LOG(error) << "PyroWave packetize failed";
      return std::nullopt;
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(num_packets * max_bytes);
    for (size_t i = 0; i < num_packets; ++i) {
      if (packets_[i].offset > bitstream_.size() ||
          packets_[i].size > bitstream_.size() - packets_[i].offset) {
        BOOST_LOG(error) << "PyroWave packet out of bounds";
        return std::nullopt;
      }
      encoded.insert(encoded.end(),
                     bitstream_.begin() + packets_[i].offset,
                     bitstream_.begin() + packets_[i].offset + packets_[i].size);
    }
    return encoded;
  }
#else
  encode_session_t::~encode_session_t() = default;
#endif
}  // namespace pyrowave
