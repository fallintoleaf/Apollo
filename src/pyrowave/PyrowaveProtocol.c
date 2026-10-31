/**
 * @file PyrowaveProtocol.c
 * @brief Bounds-checked PyroWave transport packet contract.
 */
#include "PyrowaveProtocol.h"
#include "DynamicHdr.h"

#include <string.h>

static const uint8_t PyrowaveMagic[4] = { 'P', 'Y', 'R', 'F' };

static uint16_t readU16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t readU32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           data[3];
}

static void writeU16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void writeU32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static bool validateCapabilityShape(const LI_PYROWAVE_CAPABILITIES* capabilities) {
    return capabilities != NULL &&
           capabilities->reserved == 0 &&
           capabilities->maxPacketSize >= LI_PYROWAVE_WIRE_HEADER_SIZE + 1u &&
           capabilities->maxPacketSize <= LI_PYROWAVE_MAX_PACKET_SIZE;
}

bool LiPyrowaveValidatePacketHeader(const LI_PYROWAVE_PACKET_HEADER* header) {
    uint32_t expected_group_count;
    uint16_t expected_group_data_count;
    uint64_t min_protected_payload;
    uint64_t max_protected_payload;
    const bool is_frame_header = header != NULL &&
        header->packetKind == LI_PYROWAVE_PACKET_FRAME_HEADER;
    const bool is_parity = header != NULL &&
        header->packetKind == LI_PYROWAVE_PACKET_PARITY;

    if (header == NULL || header->version != LI_PYROWAVE_PROTOCOL_VERSION ||
            header->headerLength != LI_PYROWAVE_WIRE_HEADER_SIZE ||
            (header->flags & (uint8_t)~LI_PYROWAVE_FLAG_MASK) != 0 ||
            header->packetKind > LI_PYROWAVE_PACKET_PARITY ||
            header->frameId == 0 || header->blockCount == 0 ||
            header->dataBlockCount == 0 ||
            header->dataBlockCount > header->blockCount ||
            header->parityBlockCount != (uint16_t)(header->blockCount - header->dataBlockCount) ||
            header->codecPayloadLength == 0 ||
            (header->metadataFlags & ~(LI_PYROWAVE_METADATA_FLAG_PROTECTED |
                                       LI_PYROWAVE_METADATA_FLAG_RUNTIME |
                                       LI_PYROWAVE_METADATA_FLAG_OPTIONAL |
                                       LI_PYROWAVE_METADATA_FLAG_REQUIRED)) != 0 ||
            ((header->metadataFlags & LI_PYROWAVE_METADATA_FLAG_OPTIONAL) != 0 &&
             (header->metadataFlags & LI_PYROWAVE_METADATA_FLAG_REQUIRED) != 0) ||
            (uint64_t)header->metadataLength + header->codecPayloadLength !=
                header->protectedPayloadLength ||
            header->protectedPayloadLength == 0 ||
            header->reserved != 0 || header->reserved2 != 0 ||
            header->reserved3 != 0 || header->reserved4 != 0 ||
            header->reserved5 != 0 || header->reserved6 != 0 ||
            header->fecBlockPayloadSize == 0 ||
            header->fecBlockPayloadSize > LI_PYROWAVE_MAX_PACKET_SIZE - LI_PYROWAVE_WIRE_HEADER_SIZE ||
            header->payloadLength > LI_PYROWAVE_MAX_PACKET_SIZE - LI_PYROWAVE_WIRE_HEADER_SIZE ||
            ((header->metadataLength != 0) &&
             ((header->flags & LI_PYROWAVE_FLAG_METADATA_PRESENT) == 0 ||
              (header->metadataFlags & LI_PYROWAVE_METADATA_FLAG_PROTECTED) == 0)) ||
            ((header->metadataLength == 0) &&
             ((header->flags & LI_PYROWAVE_FLAG_METADATA_PRESENT) != 0 ||
              header->metadataFlags != 0))) {
        return false;
    }

    expected_group_count = header->fecScheme == LI_PYROWAVE_FEC_SCHEME_XOR
        ? (header->dataBlockCount + LI_PYROWAVE_FEC_DATA_PER_GROUP - 1u) /
            LI_PYROWAVE_FEC_DATA_PER_GROUP
        : 0;
    min_protected_payload = (uint64_t)(header->dataBlockCount - 1u) *
        header->fecBlockPayloadSize + 1u;
    max_protected_payload = (uint64_t)header->dataBlockCount *
        header->fecBlockPayloadSize;
    if (header->protectedPayloadLength < min_protected_payload ||
            header->protectedPayloadLength > max_protected_payload) {
        return false;
    }

    if (header->fecScheme == LI_PYROWAVE_FEC_SCHEME_NONE) {
        if (header->parityBlockCount != 0 || header->dataBlockCount != header->blockCount ||
                header->fecGroupIndex != 0 || header->fecDataCount != 0 ||
                header->fecParityCount != 0 || header->fecShardIndex != 0) {
            return false;
        }
    }
    else if (header->fecScheme == LI_PYROWAVE_FEC_SCHEME_XOR) {
        if (header->parityBlockCount != expected_group_count ||
                (!is_frame_header && header->fecParityCount != LI_PYROWAVE_FEC_PARITY_SHARDS)) {
            return false;
        }
        if (!is_frame_header) {
            if (header->fecGroupIndex >= expected_group_count) {
                return false;
            }
            expected_group_data_count = (uint16_t)(header->dataBlockCount -
                header->fecGroupIndex * LI_PYROWAVE_FEC_DATA_PER_GROUP);
            if (expected_group_data_count > LI_PYROWAVE_FEC_DATA_PER_GROUP) {
                expected_group_data_count = LI_PYROWAVE_FEC_DATA_PER_GROUP;
            }
            if (header->fecDataCount != expected_group_data_count) {
                return false;
            }
        }
    }
    else {
        return false;
    }

    if (is_frame_header) {
        if (header->blockIndex != 0 || is_parity ||
                (header->flags & LI_PYROWAVE_FLAG_START_OF_FRAME) == 0 ||
                (header->flags & (LI_PYROWAVE_FLAG_END_OF_FRAME | LI_PYROWAVE_FLAG_FEC_PARITY)) != 0 ||
                header->payloadLength != header->fecBlockPayloadSize ||
                header->fecGroupIndex != 0 || header->fecDataCount != 0 ||
                header->fecParityCount != 0 || header->fecShardIndex != 0) {
            return false;
        }
        return true;
    }

    if (header->blockIndex >= header->blockCount || header->payloadLength == 0) {
        return false;
    }
    if (is_parity) {
        if ((header->flags & LI_PYROWAVE_FLAG_FEC_PARITY) == 0 ||
                header->fecScheme != LI_PYROWAVE_FEC_SCHEME_XOR ||
                header->blockIndex < header->dataBlockCount ||
                header->blockIndex != header->dataBlockCount + header->fecGroupIndex ||
                header->fecShardIndex != header->fecDataCount ||
                header->payloadLength != header->fecBlockPayloadSize) {
            return false;
        }
    }
    else {
        if ((header->flags & LI_PYROWAVE_FLAG_FEC_PARITY) != 0 ||
                header->blockIndex >= header->dataBlockCount ||
                header->payloadLength > header->fecBlockPayloadSize ||
                (header->blockIndex + 1u < header->dataBlockCount &&
                 header->payloadLength != header->fecBlockPayloadSize) ||
                (header->blockIndex + 1u == header->dataBlockCount &&
                 (uint64_t)header->payloadLength < header->protectedPayloadLength -
                     (uint64_t)header->blockIndex * header->fecBlockPayloadSize)) {
            return false;
        }
        if (header->fecScheme == LI_PYROWAVE_FEC_SCHEME_XOR &&
                (header->fecShardIndex >= header->fecDataCount ||
                 header->blockIndex != header->fecGroupIndex * LI_PYROWAVE_FEC_DATA_PER_GROUP +
                     header->fecShardIndex)) {
            return false;
        }
    }

    if (!is_parity && header->blockIndex == 0 &&
            (header->flags & LI_PYROWAVE_FLAG_START_OF_FRAME) == 0) {
        return false;
    }
    if (!is_parity && header->blockIndex + 1u == header->dataBlockCount &&
            (header->flags & LI_PYROWAVE_FLAG_END_OF_FRAME) == 0) {
        return false;
    }
    if (is_parity && (header->flags & (LI_PYROWAVE_FLAG_START_OF_FRAME |
                                       LI_PYROWAVE_FLAG_END_OF_FRAME)) != 0) {
        return false;
    }
    return true;
}

uint16_t LiPyrowaveDynamicHdrMetadataType(int format) {
    switch (format) {
    case DYNAMIC_HDR_FORMAT_HDR10_PLUS:
        return LI_PYROWAVE_METADATA_HDR10_PLUS;
    case DYNAMIC_HDR_FORMAT_VIVID_PQ:
    case DYNAMIC_HDR_FORMAT_VIVID_HLG:
        return LI_PYROWAVE_METADATA_HDR_VIVID;
    case DYNAMIC_HDR_FORMAT_DOLBY_VISION_PROFILE_81:
    case DYNAMIC_HDR_FORMAT_DOLBY_VISION_PROFILE_84:
        return LI_PYROWAVE_METADATA_DOLBY_VISION_RPU;
    default:
        return 0;
    }
}

bool LiPyrowaveRequestsDynamicHdr(int hdrMode, uint32_t dynamicHdrCaps, int dynamicHdrPreference) {
    /* Preference 3 selects static HDR10 even if the device supports dynamic HDR. */
    if (dynamicHdrPreference == 3) {
        return false;
    }
    if (dynamicHdrPreference == 2) {
        return hdrMode == 1 && (dynamicHdrCaps & DYNAMIC_HDR_CAPS_HDR10_PLUS) != 0;
    }
    if (hdrMode == 1) {
        return (dynamicHdrCaps & (DYNAMIC_HDR_CAPS_HDR10_PLUS |
                                 DYNAMIC_HDR_CAPS_VIVID_PQ |
                                 DYNAMIC_HDR_CAPS_DOLBY_VISION_81)) != 0;
    }
    if (hdrMode == 2) {
        return (dynamicHdrCaps & (DYNAMIC_HDR_CAPS_VIVID_HLG |
                                 DYNAMIC_HDR_CAPS_DOLBY_VISION_84)) != 0;
    }
    return false;
}

bool LiPyrowaveDynamicHdrMatchesMode(int format, int hdrMode) {
    switch (format) {
    case DYNAMIC_HDR_FORMAT_NONE:
        return true;
    case DYNAMIC_HDR_FORMAT_HDR10_PLUS:
    case DYNAMIC_HDR_FORMAT_VIVID_PQ:
    case DYNAMIC_HDR_FORMAT_DOLBY_VISION_PROFILE_81:
        return hdrMode == 1;
    case DYNAMIC_HDR_FORMAT_VIVID_HLG:
    case DYNAMIC_HDR_FORMAT_DOLBY_VISION_PROFILE_84:
        return hdrMode == 2;
    default:
        return false;
    }
}

bool LiPyrowaveValidateCapabilities(const LI_PYROWAVE_CAPABILITIES* capabilities) {
    if (!validateCapabilityShape(capabilities) ||
            capabilities->protocolVersion != LI_PYROWAVE_PROTOCOL_VERSION ||
            capabilities->bitstreamVersion != LI_PYROWAVE_BITSTREAM_VERSION ||
            capabilities->payloadVersion != LI_PYROWAVE_PAYLOAD_VERSION) {
        return false;
    }

    return true;
}

LI_PYROWAVE_NEGOTIATION_RESULT LiPyrowaveNegotiate(
        const LI_PYROWAVE_CAPABILITIES* server,
        const LI_PYROWAVE_CAPABILITIES* client,
        uint32_t requiredCapabilities,
        LI_PYROWAVE_CAPABILITIES* negotiated) {
    if (server == NULL || client == NULL || negotiated == NULL) {
        return LI_PYROWAVE_NEGOTIATION_INVALID_ARGUMENT;
    }
    if (!validateCapabilityShape(server) || !validateCapabilityShape(client)) {
        return LI_PYROWAVE_NEGOTIATION_INVALID_CAPABILITIES;
    }
    if (server->protocolVersion != client->protocolVersion ||
            server->bitstreamVersion != client->bitstreamVersion ||
            server->payloadVersion != client->payloadVersion ||
            server->protocolVersion != LI_PYROWAVE_PROTOCOL_VERSION ||
            server->bitstreamVersion != LI_PYROWAVE_BITSTREAM_VERSION ||
            server->payloadVersion != LI_PYROWAVE_PAYLOAD_VERSION) {
        return LI_PYROWAVE_NEGOTIATION_VERSION_MISMATCH;
    }
    if ((server->capabilityFlags & client->capabilityFlags & requiredCapabilities) !=
            requiredCapabilities) {
        return LI_PYROWAVE_NEGOTIATION_MISSING_CAPABILITY;
    }

    *negotiated = *server;
    negotiated->capabilityFlags = server->capabilityFlags & client->capabilityFlags;
    if (client->maxPacketSize < negotiated->maxPacketSize) {
        negotiated->maxPacketSize = client->maxPacketSize;
    }
    return LI_PYROWAVE_NEGOTIATION_OK;
}

LI_PYROWAVE_PACKET_RESULT LiPyrowaveBuildPacket(
        const LI_PYROWAVE_PACKET_HEADER* header,
        const uint8_t* payload,
        size_t payloadLength,
        uint8_t* output,
        size_t outputCapacity,
        size_t* outputLength) {
    size_t packetLength;

    if (header == NULL || payload == NULL || output == NULL || outputLength == NULL) {
        return LI_PYROWAVE_PACKET_INVALID_ARGUMENT;
    }
    *outputLength = 0;
    if (payloadLength > UINT16_MAX || payloadLength != header->payloadLength) {
        return LI_PYROWAVE_PACKET_INVALID_ARGUMENT;
    }
    if (!LiPyrowaveValidatePacketHeader(header)) {
        return LI_PYROWAVE_PACKET_INVALID_HEADER;
    }

    packetLength = header->headerLength + payloadLength;
    if (packetLength > LI_PYROWAVE_MAX_PACKET_SIZE || outputCapacity < packetLength) {
        return LI_PYROWAVE_PACKET_OVERSIZE;
    }

    memset(output, 0, LI_PYROWAVE_WIRE_HEADER_SIZE);
    memcpy(output, PyrowaveMagic, sizeof(PyrowaveMagic));
    output[4] = header->version;
    output[5] = header->packetKind;
    output[6] = header->flags;
    output[7] = header->reserved;
    writeU16(output + 8, header->headerLength);
    writeU16(output + 10, header->metadataFlags);
    writeU32(output + 12, header->frameId);
    writeU32(output + 16, header->rtpTimestamp);
    writeU32(output + 20, header->codecPayloadLength);
    writeU32(output + 24, header->protectedPayloadLength);
    writeU16(output + 28, header->metadataLength);
    output[30] = header->fecScheme;
    output[31] = header->reserved2;
    writeU16(output + 32, header->dataBlockCount);
    writeU16(output + 34, header->parityBlockCount);
    writeU16(output + 36, header->fecBlockPayloadSize);
    writeU16(output + 38, header->reserved3);
    writeU16(output + 40, header->blockIndex);
    writeU16(output + 42, header->blockCount);
    writeU16(output + 44, header->fecGroupIndex);
    output[46] = header->fecDataCount;
    output[47] = header->fecParityCount;
    output[48] = header->fecShardIndex;
    output[49] = header->reserved4;
    writeU16(output + 50, header->payloadLength);
    writeU32(output + 52, header->reserved5);
    memcpy(output + header->headerLength, payload, payloadLength);
    *outputLength = packetLength;
    return LI_PYROWAVE_PACKET_OK;
}

LI_PYROWAVE_PACKET_RESULT LiPyrowaveParsePacket(
        const uint8_t* packet,
        size_t packetLength,
        LI_PYROWAVE_PACKET_HEADER* header,
        const uint8_t** payload) {
    LI_PYROWAVE_PACKET_HEADER parsed;

    if (packet == NULL || header == NULL || payload == NULL) {
        return LI_PYROWAVE_PACKET_INVALID_ARGUMENT;
    }
    if (packetLength < LI_PYROWAVE_WIRE_HEADER_SIZE) {
        return LI_PYROWAVE_PACKET_TRUNCATED;
    }
    if (packetLength > LI_PYROWAVE_MAX_PACKET_SIZE) {
        return LI_PYROWAVE_PACKET_OVERSIZE;
    }
    if (memcmp(packet, PyrowaveMagic, sizeof(PyrowaveMagic)) != 0) {
        return LI_PYROWAVE_PACKET_BAD_MAGIC;
    }

    parsed.version = packet[4];
    parsed.packetKind = packet[5];
    parsed.flags = packet[6];
    parsed.reserved = packet[7];
    parsed.headerLength = readU16(packet + 8);
    parsed.metadataFlags = readU16(packet + 10);
    parsed.frameId = readU32(packet + 12);
    parsed.rtpTimestamp = readU32(packet + 16);
    parsed.codecPayloadLength = readU32(packet + 20);
    parsed.protectedPayloadLength = readU32(packet + 24);
    parsed.metadataLength = readU16(packet + 28);
    parsed.fecScheme = packet[30];
    parsed.reserved2 = packet[31];
    parsed.dataBlockCount = readU16(packet + 32);
    parsed.parityBlockCount = readU16(packet + 34);
    parsed.fecBlockPayloadSize = readU16(packet + 36);
    parsed.reserved3 = readU16(packet + 38);
    parsed.blockIndex = readU16(packet + 40);
    parsed.blockCount = readU16(packet + 42);
    parsed.fecGroupIndex = readU16(packet + 44);
    parsed.fecDataCount = packet[46];
    parsed.fecParityCount = packet[47];
    parsed.fecShardIndex = packet[48];
    parsed.reserved4 = packet[49];
    parsed.payloadLength = readU16(packet + 50);
    parsed.reserved5 = readU32(packet + 52);
    parsed.reserved6 = 0;
    {
        size_t reservedIndex;
        for (reservedIndex = 56; reservedIndex < LI_PYROWAVE_WIRE_HEADER_SIZE; ++reservedIndex) {
            if (packet[reservedIndex] != 0) {
                return LI_PYROWAVE_PACKET_INVALID_HEADER;
            }
        }
    }
    if (parsed.version != LI_PYROWAVE_PROTOCOL_VERSION) {
        return LI_PYROWAVE_PACKET_UNSUPPORTED_VERSION;
    }
    if (parsed.headerLength != LI_PYROWAVE_WIRE_HEADER_SIZE) {
        return LI_PYROWAVE_PACKET_INVALID_HEADER;
    }
    if (parsed.headerLength > packetLength) {
        return LI_PYROWAVE_PACKET_TRUNCATED;
    }
    if (parsed.payloadLength > packetLength - parsed.headerLength) {
        return LI_PYROWAVE_PACKET_TRUNCATED;
    }
    if (packetLength != (size_t)parsed.headerLength + parsed.payloadLength) {
        return LI_PYROWAVE_PACKET_INVALID_HEADER;
    }
    if (!LiPyrowaveValidatePacketHeader(&parsed)) {
        return LI_PYROWAVE_PACKET_INVALID_HEADER;
    }

    *header = parsed;
    *payload = packet + parsed.headerLength;
    return LI_PYROWAVE_PACKET_OK;
}
