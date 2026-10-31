/**
 * @file pyrowave_server.h
 * @brief Server-side PyroWave integration for Apollo.
 *
 * Wire protocol: the PYRF Frame Envelope (protocol v2 / bitstream v2 /
 * payload v3) as implemented by moonlight-common-c (mic branch) and consumed
 * by Moonlight V+ clients. Protocol constants and packet build/negotiate
 * helpers are vendored from moonlight-common-c src/PyrowaveProtocol.*.
 *
 * Encoding uses the AlkaidLab pyrowave C API (API version 101.0.0).
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include "PyrowaveProtocol.h"
}

#ifdef PYROWAVE_ENABLED
// Defined in <pyrowave/pyrowave.h> (global scope). Only the class fields
// below reference it, and they are compiled together with that header.
struct pyrowave_packet;
#endif

namespace pyrowave {

  /// Video format value carried in stream config (0 - H.264, 1 - HEVC, 2 - AV1, 3 - PyroWave).
  constexpr int INTERNAL_VIDEO_FORMAT = 3;

  /// Capability flags advertised by this server (SDR 4:2:0 + both YUV ranges, static HDR ready).
  constexpr std::uint32_t SERVER_CAPABILITY_FLAGS =
    LI_PYROWAVE_CAPABILITY_REASSEMBLY |
    LI_PYROWAVE_CAPABILITY_FRAME_DEADLINE |
    LI_PYROWAVE_CAPABILITY_BLOCK_AWARE_FEC |
    LI_PYROWAVE_CAPABILITY_FRAME_METADATA |
    LI_PYROWAVE_CAPABILITY_SDR_BT709_YUV420 |
    LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE |
    LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE |
    LI_PYROWAVE_CAPABILITY_HDR10_PQ_BT2020 |
    LI_PYROWAVE_CAPABILITY_HLG_BT2020;

  /// True when the client's bitStreamFormat value selects PyroWave.
  bool is_experimental_video_format(std::uint32_t video_format);

  /// True when this build is compiled with PyroWave support.
  bool is_compiled();

  /// True when a PyroWave Vulkan device can be created (cached after first call).
  bool is_server_runtime_available();

  /**
   * Clamp the outer RTP packet size so one PYRF packet (64-byte header +
   * payload) fits into a single outer RTP payload. Returns 0 on failure.
   */
  int limit_rtp_packet_size(int requested_packet_size, std::uint32_t max_wire_packet_size);

  struct negotiate_result_t {
    bool ok;
    std::uint32_t negotiated_max_packet_size;
  };

  /**
   * Validate client ANNOUNCE attributes against the server contract.
   * dynamic_range: 0 - SDR, 1 - HDR10/PQ, 2 - HLG.
   */
  negotiate_result_t
  negotiate_client_caps(std::uint16_t protocol_version, std::uint16_t bitstream_version,
                        std::uint16_t payload_version, std::uint32_t capability_flags,
                        std::uint32_t max_packet_size, int dynamic_range, bool full_range);

  /// Build the complete PYRF wire stream (FRAME_HEADER + DATA blocks + XOR PARITY) for one frame.
  std::optional<std::vector<std::uint8_t>>
  packetize_frame(std::uint32_t frame_id, std::uint32_t rtp_timestamp,
                  const std::uint8_t *codec_bytes, std::size_t codec_size,
                  std::size_t packet_boundary);

  /// CPU-encoding session wrapper around the pyrowave C API (when compiled
  /// with PYROWAVE_ENABLED; a stub when not).
  class encode_session_t {
  public:
    encode_session_t() = default;
    ~encode_session_t();

    encode_session_t(const encode_session_t &) = delete;
    encode_session_t &operator=(const encode_session_t &) = delete;

#ifdef PYROWAVE_ENABLED
    bool init(int width, int height, bool full_range, int bitrate_kbps, int framerate);

    /// Encode one NV12 frame. Returns the raw codec byte stream, or nullopt on failure.
    std::optional<std::vector<std::uint8_t>>
    encode(const std::uint8_t *y_plane, std::size_t y_stride,
           const std::uint8_t *uv_plane, std::size_t uv_stride,
           std::size_t max_bytes);
#endif

  private:
#ifdef PYROWAVE_ENABLED
    void destroy();

    int width_ = 0;
    int height_ = 0;
    void *device_ = nullptr;
    void *encoder_ = nullptr;
    std::vector<std::uint8_t> bitstream_;
    std::vector<pyrowave_packet> packets_;
#endif
  };
}  // namespace pyrowave
