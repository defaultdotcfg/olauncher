#pragma once

#include <d3d12.h>
#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

// ============================================================================
// Xbox Sampler Packed Header (8 bytes)
// As observed at offset 0x00 in the 104-byte container allocated by the game.
// ============================================================================
struct XboxSamplerPackedHeader
{
    uint8_t filter_idx;      // 0..10
    uint8_t addr_u_idx;      // 0..7
    uint8_t addr_v_idx;      // 0..7
    uint8_t addr_w_idx;      // 0..7
    uint8_t comp_func_idx;   // 0..7
    uint8_t reserved[3];
};
static_assert(sizeof(XboxSamplerPackedHeader) == 8, "XboxSamplerPackedHeader must be 8 bytes");

// ============================================================================
// Xbox Sampler Payload (96 bytes)
// Received by CreateSampler (pDesc) at container + 0x08.
// ============================================================================
struct XboxSamplerPayload
{
    uint32_t FilterTag;       // +0x00: Initialized to 0x20
    uint32_t MinFilter;       // +0x04: d0 from table 0xE4C260
    uint32_t MagFilter;       // +0x08: d0 from table 0xE4C260
    uint32_t MipFilter;       // +0x0C: d1 from table 0xE4C260
    uint32_t MipMode;         // +0x10: d2 from table 0xE4C260 (1=mono-level, 2=multi-level)
    float    MipLODBias;      // +0x14: Base LOD bias (typically 0.0f)
    uint32_t AddressU;        // +0x18: Translated from table 0xE4ED38
    uint32_t AddressV;        // +0x1C: Translated from table 0xE4ED38
    uint32_t AddressW;        // +0x20: Translated from table 0xE4ED38
    float    LODBiasAdj;      // +0x24: LOD bias adjustment (typically 0.0f)
    float    MaxLOD;          // +0x28: Maximum LOD (typically FLT_MAX = 0x7F7FFFFF)
    uint32_t Unk2C;           // +0x2C: 0
    uint32_t Unk30;           // +0x30: 0
    uint32_t MaxAnisotropy;   // +0x34: Max anisotropy (1, 4, 8, 16)
    float    MinLOD;          // +0x38: Minimum LOD (0.0f or 0.5f)
    uint32_t QualityTier;     // +0x3C: 0
    uint32_t ComparisonFunc;  // +0x40: Translated from table 0xE4ED48
    uint32_t Unk44;           // +0x44: 0
    float    BorderColorR;    // +0x48: 0.0f
    float    BorderColorG;    // +0x4C: 0.0f
    float    BorderColorB;    // +0x50: 0.0f
    uint32_t FlagsOrBias;     // +0x54: Integer flags / bias counter (0 or 7)
    uint32_t Unk58;           // +0x58: 0
    uint32_t Padding;         // +0x5C: 4 bytes non-semantic padding
};
static_assert(sizeof(XboxSamplerPayload) == 96, "XboxSamplerPayload must be 96 bytes");
static_assert(offsetof(XboxSamplerPayload, FilterTag) == 0x00, "FilterTag offset");
static_assert(offsetof(XboxSamplerPayload, MinFilter) == 0x04, "MinFilter offset");
static_assert(offsetof(XboxSamplerPayload, MagFilter) == 0x08, "MagFilter offset");
static_assert(offsetof(XboxSamplerPayload, MipFilter) == 0x0C, "MipFilter offset");
static_assert(offsetof(XboxSamplerPayload, MipMode) == 0x10, "MipMode offset");
static_assert(offsetof(XboxSamplerPayload, MipLODBias) == 0x14, "MipLODBias offset");
static_assert(offsetof(XboxSamplerPayload, AddressU) == 0x18, "AddressU offset");
static_assert(offsetof(XboxSamplerPayload, AddressV) == 0x1C, "AddressV offset");
static_assert(offsetof(XboxSamplerPayload, AddressW) == 0x20, "AddressW offset");
static_assert(offsetof(XboxSamplerPayload, LODBiasAdj) == 0x24, "LODBiasAdj offset");
static_assert(offsetof(XboxSamplerPayload, MaxLOD) == 0x28, "MaxLOD offset");
static_assert(offsetof(XboxSamplerPayload, MaxAnisotropy) == 0x34, "MaxAnisotropy offset");
static_assert(offsetof(XboxSamplerPayload, MinLOD) == 0x38, "MinLOD offset");
static_assert(offsetof(XboxSamplerPayload, ComparisonFunc) == 0x40, "ComparisonFunc offset");
static_assert(offsetof(XboxSamplerPayload, BorderColorR) == 0x48, "BorderColorR offset");
static_assert(offsetof(XboxSamplerPayload, FlagsOrBias) == 0x54, "FlagsOrBias offset");
static_assert(offsetof(XboxSamplerPayload, Padding) == 0x5C, "Padding offset");

// ============================================================================
// Xbox Sampler Container (104 bytes = 0x68)
// Full structure allocated by game at 0x1400A8CA0.
// ============================================================================
struct XboxSamplerContainer
{
    XboxSamplerPackedHeader header;   // +0x00 (8 bytes)
    XboxSamplerPayload      payload;  // +0x08 (96 bytes)
};
static_assert(sizeof(XboxSamplerContainer) == 104, "XboxSamplerContainer must be 104 bytes");
static_assert(offsetof(XboxSamplerContainer, payload) == 0x08, "payload offset in container");

#pragma pack(pop)

// ============================================================================
// Translation Status
// Explicit status codes for translation and validation.
// ============================================================================
enum class SamplerTranslationStatus : uint32_t
{
    Success = 0,
    NullPointer,
    InvalidFilterTag,
    InvalidFilterIndex,
    IncompatibleMipModeOrFilterFields,
    InvalidAddressModeZero,
    UnsupportedAddressModeBorder,
    InvalidAddressMode,
    InvalidAddressModeIndex,
    InvalidComparisonFunc,
    InvalidComparisonFuncIndex,
    InvalidAnisotropy,
    NonFiniteFloat,
    InvalidLODRange,
    HeaderPayloadFilterMismatch,
    HeaderPayloadAddressMismatch,
    HeaderPayloadComparisonMismatch
};

const char* SamplerTranslationStatusToString(SamplerTranslationStatus status);

// ============================================================================
// Table Definitions (Proven from game_release_xbox.exe .rdata)
// ============================================================================
struct XboxFilterEntry
{
    uint32_t d0;                 // Min & Mag filter (0=POINT, 1=LINEAR, 3=ANISO)
    uint32_t d1;                 // Mip filter (0=NONE, 1=POINT, 2=LINEAR)
    uint32_t d2;                 // MipMode (1=mono-level, 2=multi-level)
    uint32_t reduction;          // 0=STANDARD, 1=COMPARISON
    D3D12_FILTER expectedFilter; // Encoded D3D12_FILTER
    uint32_t defaultAnisotropy;  // Default MaxAnisotropy
};

// Table 0x140E4C260 in game_release_xbox.exe
extern const XboxFilterEntry kXboxFilterTable[11];

// Table 0x140E4ED38 in game_release_xbox.exe (Packed Address Index -> Table Value)
extern const uint32_t kXboxAddressModeTable[8];

// Table 0x140E4ED48 in game_release_xbox.exe (Packed Comparison Index -> D3D12 Comparison)
extern const uint32_t kXboxComparisonTable[8];

// ============================================================================
// Core Translation Functions
// ============================================================================

// Translates a packed address index (0..7) using table 0xE4ED38.
// Rejects index 3 (produces 0) and index 7 (produces 4 / BORDER).
// Rejects values other than 1 (WRAP), 2 (MIRROR), 3 (CLAMP).
SamplerTranslationStatus TranslateXboxPackedAddressIndex(
    uint8_t packed_idx,
    D3D12_TEXTURE_ADDRESS_MODE* pOutMode
);

// Translates directly from filter_idx and expanded payload.
// Validates FilterTag == 0x20.
// Addresses in payload are treated as already translated (no double lookup).
// Rejects 0, 4 (BORDER), 5 (MIRROR_ONCE) or any value other than 1, 2, 3.
SamplerTranslationStatus TranslateXboxSamplerDesc(
    uint32_t filter_idx,
    const XboxSamplerPayload* pIn,
    D3D12_SAMPLER_DESC* pOut
);

// Translates given explicit packed header and payload, validating coherence:
// - header.filter_idx vs MinFilter/MagFilter/MipFilter/MipMode;
// - header.addr_u/v/w_idx vs AddressU/V/W;
// - header.comp_func_idx vs ComparisonFunc.
// Does NOT apply table lookup twice to payload values.
SamplerTranslationStatus TranslateXboxSampler(
    const XboxSamplerPackedHeader* pHeader,
    const XboxSamplerPayload* pPayload,
    D3D12_SAMPLER_DESC* pOut
);

// Translates full 104-byte container.
SamplerTranslationStatus TranslateXboxSamplerContainer(
    const XboxSamplerContainer* pContainer,
    D3D12_SAMPLER_DESC* pOut
);

// Helper for when only the payload pointer (pDesc) is available,
// reading the container header at (pDesc - 8).
// Caller evidence: In 0x1400A8CA0 and 0x1400B3C88, the container is contiguous
// and pDesc is always (container + 8).
SamplerTranslationStatus TranslateXboxSamplerFromPayloadPointer(
    const void* pDesc,
    D3D12_SAMPLER_DESC* pOut
);
