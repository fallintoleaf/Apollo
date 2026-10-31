/**
 * @file PyrowaveProtocol.h
 * @brief Transport contract for the experimental PyroWave video path.
 *
 * This header only defines the common-c wire contract. It does not advertise
 * PyroWave during normal negotiation and does not change legacy codecs.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The value is intentionally outside the legacy 16-bit codec mask. */
#define LI_PYROWAVE_VIDEO_FORMAT 0x00010000u
#define LI_PYROWAVE_PROTOCOL_VERSION 2u
#define LI_PYROWAVE_BITSTREAM_VERSION 2u
#define LI_PYROWAVE_PAYLOAD_VERSION 3u

#define LI_PYROWAVE_WIRE_HEADER_SIZE 64u
#define LI_PYROWAVE_WIRE_FEC_HEADER_SIZE LI_PYROWAVE_WIRE_HEADER_SIZE
#define LI_PYROWAVE_MAX_PACKET_SIZE (64u * 1024u)
#define LI_PYROWAVE_MAX_FRAME_BLOCKS 65535u
#define LI_PYROWAVE_MAX_METADATA_SIZE 65535u
#define LI_PYROWAVE_FEC_DATA_PER_GROUP 16u
/* Payload v3 currently defines one XOR parity shard per group. */
#define LI_PYROWAVE_FEC_PARITY_SHARDS 1u
#define LI_PYROWAVE_FEC_SCHEME_NONE 0u
#define LI_PYROWAVE_FEC_SCHEME_XOR 1u

#define LI_PYROWAVE_FLAG_START_OF_FRAME 0x01u
#define LI_PYROWAVE_FLAG_END_OF_FRAME   0x02u
#define LI_PYROWAVE_FLAG_CRITICAL      0x04u
#define LI_PYROWAVE_FLAG_FEC_PARITY    0x08u
#define LI_PYROWAVE_FLAG_METADATA_PRESENT 0x10u
#define LI_PYROWAVE_FLAG_MASK (LI_PYROWAVE_FLAG_START_OF_FRAME | \
                               LI_PYROWAVE_FLAG_END_OF_FRAME | \
                               LI_PYROWAVE_FLAG_CRITICAL | \
                               LI_PYROWAVE_FLAG_FEC_PARITY | \
                               LI_PYROWAVE_FLAG_METADATA_PRESENT)

typedef enum _LI_PYROWAVE_PACKET_KIND {
    LI_PYROWAVE_PACKET_FRAME_HEADER = 0,
    LI_PYROWAVE_PACKET_DATA = 1,
    LI_PYROWAVE_PACKET_PARITY = 2,
} LI_PYROWAVE_PACKET_KIND;

typedef enum _LI_PYROWAVE_METADATA_TYPE {
    LI_PYROWAVE_METADATA_HDR10_PLUS = 0x0001,
    LI_PYROWAVE_METADATA_HDR_STATIC_SNAPSHOT = 0x0002,
    LI_PYROWAVE_METADATA_COLOR_CONTRACT = 0x0003,
    LI_PYROWAVE_METADATA_HDR_VIVID = 0x0004,
    LI_PYROWAVE_METADATA_DOLBY_VISION_RPU = 0x0005,
    LI_PYROWAVE_METADATA_HLG_NOMINAL_PEAK = 0x0006,
    LI_PYROWAVE_METADATA_HOST_PROCESSING_LATENCY = 0x0100,
    LI_PYROWAVE_METADATA_FRAME_DEADLINE = 0x0101,
    LI_PYROWAVE_METADATA_TRANSPORT_STATUS = 0x0102,
} LI_PYROWAVE_METADATA_TYPE;

#define LI_PYROWAVE_METADATA_FLAG_PROTECTED 0x0001u
#define LI_PYROWAVE_METADATA_FLAG_RUNTIME   0x0002u
#define LI_PYROWAVE_METADATA_FLAG_OPTIONAL  0x0004u
#define LI_PYROWAVE_METADATA_FLAG_REQUIRED  0x0008u

/*
 * Capabilities are an application-level contract. They are not added to the
 * legacy VIDEO_FORMAT masks until the client can actually decode PyroWave.
 */
#define LI_PYROWAVE_CAPABILITY_REASSEMBLY       (1u << 0)
#define LI_PYROWAVE_CAPABILITY_FRAME_DEADLINE   (1u << 1)
#define LI_PYROWAVE_CAPABILITY_PARTIAL_FRAME    (1u << 2)
#define LI_PYROWAVE_CAPABILITY_BLOCK_AWARE_FEC  (1u << 3)
#define LI_PYROWAVE_CAPABILITY_SDR_BT709_YUV420 (1u << 4)
#define LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE  (1u << 5)
#define LI_PYROWAVE_CAPABILITY_HDR10_PQ_BT2020 (1u << 6)
#define LI_PYROWAVE_CAPABILITY_HLG_BT2020      (1u << 7)
#define LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE (1u << 8)
#define LI_PYROWAVE_CAPABILITY_FRAME_METADATA  (1u << 9)
#define LI_PYROWAVE_CAPABILITY_DYNAMIC_HDR_MAPPING (1u << 10)

/* Compatibility aliases for callers that used the first full-range-only
   contract. The wire bit now describes the negotiated YUV range, not SDR. */
#define LI_PYROWAVE_CAPABILITY_SDR_FULL_RANGE LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE
#define LI_PYROWAVE_CAPABILITY_SDR_LIMITED_RANGE LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE

#define LI_PYROWAVE_REQUIRED_SDR_BASE_CAPABILITIES \
    (LI_PYROWAVE_CAPABILITY_REASSEMBLY | \
     LI_PYROWAVE_CAPABILITY_FRAME_DEADLINE | \
     LI_PYROWAVE_CAPABILITY_BLOCK_AWARE_FEC | \
     LI_PYROWAVE_CAPABILITY_FRAME_METADATA | \
     LI_PYROWAVE_CAPABILITY_SDR_BT709_YUV420)

#define LI_PYROWAVE_REQUIRED_HDR10_BASE_CAPABILITIES \
    (LI_PYROWAVE_CAPABILITY_REASSEMBLY | \
     LI_PYROWAVE_CAPABILITY_FRAME_DEADLINE | \
     LI_PYROWAVE_CAPABILITY_BLOCK_AWARE_FEC | \
     LI_PYROWAVE_CAPABILITY_FRAME_METADATA | \
     LI_PYROWAVE_CAPABILITY_HDR10_PQ_BT2020)

#define LI_PYROWAVE_REQUIRED_HLG_BASE_CAPABILITIES \
    (LI_PYROWAVE_CAPABILITY_REASSEMBLY | \
     LI_PYROWAVE_CAPABILITY_FRAME_DEADLINE | \
     LI_PYROWAVE_CAPABILITY_BLOCK_AWARE_FEC | \
     LI_PYROWAVE_CAPABILITY_FRAME_METADATA | \
     LI_PYROWAVE_CAPABILITY_HLG_BT2020)

#define LI_PYROWAVE_REQUIRED_SDR_CAPABILITIES \
    (LI_PYROWAVE_REQUIRED_SDR_BASE_CAPABILITIES | \
     LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE)

#define LI_PYROWAVE_REQUIRED_HDR10_CAPABILITIES \
    (LI_PYROWAVE_REQUIRED_HDR10_BASE_CAPABILITIES | \
     LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE)

#define LI_PYROWAVE_REQUIRED_HLG_CAPABILITIES \
    (LI_PYROWAVE_REQUIRED_HLG_BASE_CAPABILITIES | \
     LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE)

/* Kept as the default contract for callers that have not selected HDR. */
#define LI_PYROWAVE_REQUIRED_CAPABILITIES LI_PYROWAVE_REQUIRED_SDR_CAPABILITIES

typedef enum _LI_PYROWAVE_PACKET_RESULT {
    LI_PYROWAVE_PACKET_OK = 0,
    LI_PYROWAVE_PACKET_INVALID_ARGUMENT = -1,
    LI_PYROWAVE_PACKET_TRUNCATED = -2,
    LI_PYROWAVE_PACKET_BAD_MAGIC = -3,
    LI_PYROWAVE_PACKET_UNSUPPORTED_VERSION = -4,
    LI_PYROWAVE_PACKET_INVALID_HEADER = -5,
    LI_PYROWAVE_PACKET_OVERSIZE = -6,
} LI_PYROWAVE_PACKET_RESULT;

/* Parsed fields are host-endian. The wire representation is big-endian. */
typedef struct _LI_PYROWAVE_PACKET_HEADER {
    uint8_t version;
    uint8_t packetKind;
    uint8_t flags;
    uint8_t reserved;
    uint16_t headerLength;
    uint16_t metadataFlags;
    uint32_t frameId;
    uint32_t rtpTimestamp;
    uint32_t codecPayloadLength;
    uint32_t protectedPayloadLength;
    uint16_t metadataLength;
    uint8_t fecScheme;
    uint8_t reserved2;
    uint16_t dataBlockCount;
    uint16_t parityBlockCount;
    uint16_t fecBlockPayloadSize;
    uint16_t reserved3;
    uint16_t blockIndex;
    uint16_t blockCount;
    uint16_t fecGroupIndex;
    uint8_t fecDataCount;
    uint8_t fecParityCount;
    uint8_t fecShardIndex;
    uint8_t reserved4;
    uint16_t payloadLength;
    uint32_t reserved5;
    uint64_t reserved6;
} LI_PYROWAVE_PACKET_HEADER;

typedef struct _LI_PYROWAVE_CAPABILITIES {
    uint16_t protocolVersion;
    uint16_t bitstreamVersion;
    uint16_t payloadVersion;
    uint16_t reserved;
    uint32_t capabilityFlags;
    /* Maximum complete wire packet, including the transport header. */
    uint32_t maxPacketSize;
} LI_PYROWAVE_CAPABILITIES;

typedef enum _LI_PYROWAVE_NEGOTIATION_RESULT {
    LI_PYROWAVE_NEGOTIATION_OK = 0,
    LI_PYROWAVE_NEGOTIATION_INVALID_ARGUMENT = -1,
    LI_PYROWAVE_NEGOTIATION_INVALID_CAPABILITIES = -2,
    LI_PYROWAVE_NEGOTIATION_VERSION_MISMATCH = -3,
    LI_PYROWAVE_NEGOTIATION_MISSING_CAPABILITY = -4,
} LI_PYROWAVE_NEGOTIATION_RESULT;

/* Validate fields before serialization or after parsing. */
bool LiPyrowaveValidatePacketHeader(const LI_PYROWAVE_PACKET_HEADER* header);

/* Validate a local capability descriptor before advertising or consuming it. */
bool LiPyrowaveValidateCapabilities(const LI_PYROWAVE_CAPABILITIES* capabilities);

/* Return the frame TLV type for a DynamicHdr.h format, or zero for none. */
uint16_t LiPyrowaveDynamicHdrMetadataType(int format);

/* Check the current request, not merely the presence of device capabilities. */
bool LiPyrowaveRequestsDynamicHdr(int hdrMode, uint32_t dynamicHdrCaps, int dynamicHdrPreference);

/* Dynamic formats must retain their negotiated PQ or HLG base signal. */
bool LiPyrowaveDynamicHdrMatchesMode(int format, int hdrMode);

/*
 * Compute the capability intersection without changing either input. The
 * caller supplies the flags required by the selected experiment; a failure
 * means the caller must keep the existing codec or reject the session before
 * media starts.
 */
LI_PYROWAVE_NEGOTIATION_RESULT LiPyrowaveNegotiate(
    const LI_PYROWAVE_CAPABILITIES* server,
    const LI_PYROWAVE_CAPABILITIES* client,
    uint32_t requiredCapabilities,
    LI_PYROWAVE_CAPABILITIES* negotiated);

/* Serialize one transport packet without allocating memory. */
LI_PYROWAVE_PACKET_RESULT LiPyrowaveBuildPacket(
    const LI_PYROWAVE_PACKET_HEADER* header,
    const uint8_t* payload,
    size_t payloadLength,
    uint8_t* output,
    size_t outputCapacity,
    size_t* outputLength);

/* Parse one transport packet without taking ownership of its payload. */
LI_PYROWAVE_PACKET_RESULT LiPyrowaveParsePacket(
    const uint8_t* packet,
    size_t packetLength,
    LI_PYROWAVE_PACKET_HEADER* header,
    const uint8_t** payload);

#ifdef __cplusplus
}
#endif
