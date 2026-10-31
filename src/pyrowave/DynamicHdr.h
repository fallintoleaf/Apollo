//
// Dynamic HDR negotiation wire constants (Sunshine protocol extension).
//
// Opt-in header: include it only where these values are constructed or
// parsed (e.g. RtspConnection.c). They deliberately do not live in
// Limelight.h because object-like macros with these names collide textually
// with the Sunshine host's C++ enumerators in hdr/dynamic_hdr_selection.h
// when both headers meet in one translation unit.
//

#pragma once

// Dynamic HDR format values for STREAM_CONFIGURATION.dynamicHdrCaps bits,
// LiGetNegotiatedDynamicHdrFormat() results, and the X-SS-Dynamic-HDR wire
// value. These match the Sunshine host's dynamic HDR selection.
#define DYNAMIC_HDR_CAPS_HDR10_PLUS (1 << 0)
#define DYNAMIC_HDR_CAPS_VIVID_PQ (1 << 1)
#define DYNAMIC_HDR_CAPS_VIVID_HLG (1 << 2)
#define DYNAMIC_HDR_CAPS_DOLBY_VISION_81 (1 << 3)
#define DYNAMIC_HDR_CAPS_DOLBY_VISION_84 (1 << 4)

#define DYNAMIC_HDR_FORMAT_NONE 0
#define DYNAMIC_HDR_FORMAT_HDR10_PLUS 1
#define DYNAMIC_HDR_FORMAT_VIVID_PQ 2
#define DYNAMIC_HDR_FORMAT_VIVID_HLG 3
#define DYNAMIC_HDR_FORMAT_DOLBY_VISION_PROFILE_81 4
#define DYNAMIC_HDR_FORMAT_DOLBY_VISION_PROFILE_84 5

// Fallback identities are stable: a removed member leaves a gap instead of
// shifting the rest (1 was host_disabled, retired when the host opened the
// negotiation unconditionally). Unknown names map to DYNAMIC_HDR_FALLBACK_NONE.
#define DYNAMIC_HDR_FALLBACK_NONE 0
#define DYNAMIC_HDR_FALLBACK_CODEC_UNSUPPORTED 2
#define DYNAMIC_HDR_FALLBACK_COLORSPACE_UNSUPPORTED 3
#define DYNAMIC_HDR_FALLBACK_CLIENT_CAPS_MISSING 4
#define DYNAMIC_HDR_FALLBACK_DIRECT_SURFACE_MISSING 5
#define DYNAMIC_HDR_FALLBACK_PREFERENCE 6
