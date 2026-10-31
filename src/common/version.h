// LBN Coop SA - original co-op mod for GTA San Andreas 1.0 US
// Copyright (c) 2026 LBN. All rights reserved unless stated otherwise.
#pragma once

#define LBN_COOP_NAME        "LBN Coop SA"
// Mod version (semver). Host and client must match EXACTLY; the handshake rejects any difference.
// Keep in sync with dist/LbnCoopSA.version (scripts/build_all.sh writes it from this value).
#define LBN_MOD_VERSION_MAJOR 0
#define LBN_MOD_VERSION_MINOR 1
#define LBN_MOD_VERSION_PATCH 0
#define LBN_MOD_VERSION      "0.1.0"
#define LBN_COOP_VERSION     LBN_MOD_VERSION
#define LBN_WIDEN2_(x) L##x
#define LBN_WIDEN_(x) LBN_WIDEN2_(x)
#define LBN_MOD_VERSION_W    LBN_WIDEN_(LBN_MOD_VERSION)
#define LBN_DEFAULT_PORT     7810
#define LBN_MAX_PLAYERS      5      // host + 4
#define LBN_PROTOCOL_VERSION 2      // 2: Hello carries the mod version string
#define LBN_PROTOCOL_MAGIC   0x434E424Cu  // "LBNC" little-endian
