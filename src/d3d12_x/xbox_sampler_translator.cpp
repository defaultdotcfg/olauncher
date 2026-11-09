#include "xbox_sampler_translator.h"
#include <cmath>
#include <cfloat>

// ============================================================================
// Table 0x140E4C260 in game_release_xbox.exe .rdata
// Defines the 11 valid filter combinations and their exact mappings.
// ============================================================================
const XboxFilterEntry kXboxFilterTable[11] = {
    // idx 0: d0=0, d1=0, d2=1 -> 0x00 (MIN_MAG_MIP_POINT)
    { 0, 0, 1, 0, D3D12_FILTER_MIN_MAG_MIP_POINT, 1 },
    // idx 1: d0=0, d1=2, d2=2 -> 0x01 (MIN_MAG_POINT_MIP_LINEAR)
    { 0, 2, 2, 0, D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR, 1 },
    // idx 2: d0=1, d1=1, d2=2 -> 0x14 (MIN_MAG_LINEAR_MIP_POINT)
    { 1, 1, 2, 0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, 1 },
    // idx 3: d0=1, d1=2, d2=2 -> 0x15 (MIN_MAG_MIP_LINEAR)
    { 1, 2, 2, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, 1 },
    // idx 4: d0=3, d1=2, d2=2 -> 0x55 (ANISOTROPIC)
    { 3, 2, 2, 0, D3D12_FILTER_ANISOTROPIC, 16 },
    // idx 5: d0=0, d1=0, d2=1 -> 0x80 (COMPARISON_MIN_MAG_MIP_POINT)
    { 0, 0, 1, 1, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT, 1 },
    // idx 6: d0=1, d1=1, d2=2 -> 0x94 (COMPARISON_MIN_MAG_LINEAR_MIP_POINT)
    { 1, 1, 2, 1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, 1 },
    // idx 7: d0=1, d1=2, d2=2 -> 0x95 (COMPARISON_MIN_MAG_MIP_LINEAR)
    { 1, 2, 2, 1, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, 1 },
    // idx 8: d0=1, d1=2, d2=2 -> 0x95 (COMPARISON_MIN_MAG_MIP_LINEAR)
    { 1, 2, 2, 1, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, 1 },
    // idx 9: d0=3, d1=2, d2=2 -> 0xD5 (COMPARISON_ANISOTROPIC)
    { 3, 2, 2, 1, D3D12_FILTER_COMPARISON_ANISOTROPIC, 8 },
    // idx 10: d0=3, d1=2, d2=2 -> 0xD5 (COMPARISON_ANISOTROPIC)
    { 3, 2, 2, 1, D3D12_FILTER_COMPARISON_ANISOTROPIC, 4 }
};

// ============================================================================
// Table 0x140E4ED38 in game_release_xbox.exe .rdata
// Lookup for packed address mode index (0..7) -> Table Value.
// ============================================================================
const uint32_t kXboxAddressModeTable[8] = {
    3, // 0 -> CLAMP (3)
    1, // 1 -> WRAP (1)
    2, // 2 -> MIRROR (2)
    0, // 3 -> INVALID (0) [Rejection required]
    1, // 4 -> WRAP (1)
    2, // 5 -> MIRROR (2)
    3, // 6 -> CLAMP (3)
    4  // 7 -> BORDER (4) [Rejection required: color unproven]
};

// ============================================================================
// Table 0x140E4ED48 in game_release_xbox.exe .rdata
// Lookup for packed comparison function index (0..7) -> D3D12 comparison enum.
// ============================================================================
const uint32_t kXboxComparisonTable[8] = {
    1, // 0 -> NEVER (1)
    2, // 1 -> LESS (2)
    3, // 2 -> EQUAL (3)
    4, // 3 -> LESS_EQUAL (4)
    5, // 4 -> GREATER (5)
    6, // 5 -> NOT_EQUAL (6)
    7, // 6 -> GREATER_EQUAL (7)
    8  // 7 -> ALWAYS (8)
};

// ============================================================================
// Status String Helper
// ============================================================================
const char* SamplerTranslationStatusToString(SamplerTranslationStatus status)
{
    switch (status)
    {
    case SamplerTranslationStatus::Success: return "Success";
    case SamplerTranslationStatus::NullPointer: return "NullPointer";
    case SamplerTranslationStatus::InvalidFilterTag: return "InvalidFilterTag";
    case SamplerTranslationStatus::InvalidFilterIndex: return "InvalidFilterIndex";
    case SamplerTranslationStatus::IncompatibleMipModeOrFilterFields: return "IncompatibleMipModeOrFilterFields";
    case SamplerTranslationStatus::InvalidAddressModeZero: return "InvalidAddressModeZero";
    case SamplerTranslationStatus::UnsupportedAddressModeBorder: return "UnsupportedAddressModeBorder";
    case SamplerTranslationStatus::InvalidAddressMode: return "InvalidAddressMode";
    case SamplerTranslationStatus::InvalidAddressModeIndex: return "InvalidAddressModeIndex";
    case SamplerTranslationStatus::InvalidComparisonFunc: return "InvalidComparisonFunc";
    case SamplerTranslationStatus::InvalidComparisonFuncIndex: return "InvalidComparisonFuncIndex";
    case SamplerTranslationStatus::InvalidAnisotropy: return "InvalidAnisotropy";
    case SamplerTranslationStatus::NonFiniteFloat: return "NonFiniteFloat";
    case SamplerTranslationStatus::InvalidLODRange: return "InvalidLODRange";
    case SamplerTranslationStatus::HeaderPayloadFilterMismatch: return "HeaderPayloadFilterMismatch";
    case SamplerTranslationStatus::HeaderPayloadAddressMismatch: return "HeaderPayloadAddressMismatch";
    case SamplerTranslationStatus::HeaderPayloadComparisonMismatch: return "HeaderPayloadComparisonMismatch";
    default: return "Unknown";
    }
}

// ============================================================================
// Translate Packed Address Index (0..7)
// ============================================================================
SamplerTranslationStatus TranslateXboxPackedAddressIndex(
    uint8_t packed_idx,
    D3D12_TEXTURE_ADDRESS_MODE* pOutMode)
{
    if (!pOutMode)
    {
        return SamplerTranslationStatus::NullPointer;
    }

    if (packed_idx >= 8)
    {
        return SamplerTranslationStatus::InvalidAddressModeIndex;
    }

    uint32_t rawVal = kXboxAddressModeTable[packed_idx];

    // Explicitly reject index 3 (which produces 0, an invalid D3D12 enum)
    if (rawVal == 0)
    {
        return SamplerTranslationStatus::InvalidAddressModeZero;
    }

    // Explicitly reject BORDER (4) because border color is unproven
    if (rawVal == 4)
    {
        return SamplerTranslationStatus::UnsupportedAddressModeBorder;
    }

    // Strict validation: Table 0xE4ED38 only produces 1 (WRAP), 2 (MIRROR), 3 (CLAMP).
    // Rejects 5 (MIRROR_ONCE) or any unexpected values.
    if (rawVal != 1 && rawVal != 2 && rawVal != 3)
    {
        return SamplerTranslationStatus::InvalidAddressMode;
    }

    *pOutMode = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(rawVal);
    return SamplerTranslationStatus::Success;
}

// Internal helper for validating already-translated address mode from payload
static SamplerTranslationStatus ValidatePayloadAddressMode(uint32_t mode, D3D12_TEXTURE_ADDRESS_MODE* pOut)
{
    if (mode == 0)
    {
        return SamplerTranslationStatus::InvalidAddressModeZero;
    }
    if (mode == 4)
    {
        return SamplerTranslationStatus::UnsupportedAddressModeBorder;
    }
    // Strict validation: Only accept 1 (WRAP), 2 (MIRROR), 3 (CLAMP).
    // Reject 5 (MIRROR_ONCE) because table 0xE4ED38 never produces it.
    if (mode != 1 && mode != 2 && mode != 3)
    {
        return SamplerTranslationStatus::InvalidAddressMode;
    }
    *pOut = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(mode);
    return SamplerTranslationStatus::Success;
}

// ============================================================================
// TranslateXboxSamplerDesc
// ============================================================================
SamplerTranslationStatus TranslateXboxSamplerDesc(
    uint32_t filter_idx,
    const XboxSamplerPayload* pIn,
    D3D12_SAMPLER_DESC* pOut)
{
    if (!pIn || !pOut)
    {
        return SamplerTranslationStatus::NullPointer;
    }

    // 1. Validate FilterTag == 0x20
    if (pIn->FilterTag != 0x20)
    {
        return SamplerTranslationStatus::InvalidFilterTag;
    }

    // 2. Validate filter index (0..10)
    if (filter_idx >= 11)
    {
        return SamplerTranslationStatus::InvalidFilterIndex;
    }

    const XboxFilterEntry& entry = kXboxFilterTable[filter_idx];

    // 3. Validate that payload fields (d0, d1, d2) are compatible with the proven table
    if (pIn->MinFilter != entry.d0 ||
        pIn->MagFilter != entry.d0 ||
        pIn->MipFilter != entry.d1 ||
        pIn->MipMode   != entry.d2)
    {
        return SamplerTranslationStatus::IncompatibleMipModeOrFilterFields;
    }

    // 4. Set Filter enum directly from proven table
    pOut->Filter = entry.expectedFilter;

    // 5. Validate and set Address Modes (no second lookup, already translated)
    SamplerTranslationStatus status;
    status = ValidatePayloadAddressMode(pIn->AddressU, &pOut->AddressU);
    if (status != SamplerTranslationStatus::Success)
    {
        return status;
    }

    status = ValidatePayloadAddressMode(pIn->AddressV, &pOut->AddressV);
    if (status != SamplerTranslationStatus::Success)
    {
        return status;
    }

    status = ValidatePayloadAddressMode(pIn->AddressW, &pOut->AddressW);
    if (status != SamplerTranslationStatus::Success)
    {
        return status;
    }

    // 6. Calculate and validate MipLODBias (base + adjustment)
    float baseBias = pIn->MipLODBias;
    float adjBias = pIn->LODBiasAdj;
    if (std::isnan(baseBias) || std::isinf(baseBias) ||
        std::isnan(adjBias)  || std::isinf(adjBias))
    {
        return SamplerTranslationStatus::NonFiniteFloat;
    }

    float totalBias = baseBias + adjBias;
    if (std::isnan(totalBias) || std::isinf(totalBias))
    {
        return SamplerTranslationStatus::NonFiniteFloat;
    }

    // Clamp effective bias to documented D3D12 limits [-16.0f, 15.99f]
    if (totalBias < -16.0f)
    {
        totalBias = -16.0f;
    }
    else if (totalBias > 15.99f)
    {
        totalBias = 15.99f;
    }
    pOut->MipLODBias = totalBias;

    // 7. Validate MaxAnisotropy
    if (entry.expectedFilter == D3D12_FILTER_ANISOTROPIC ||
        entry.expectedFilter == D3D12_FILTER_COMPARISON_ANISOTROPIC)
    {
        uint32_t aniso = pIn->MaxAnisotropy;
        if (aniso < 1 || aniso > 16)
        {
            return SamplerTranslationStatus::InvalidAnisotropy;
        }
        pOut->MaxAnisotropy = aniso;
    }
    else
    {
        pOut->MaxAnisotropy = 1;
    }

    // 8. Validate ComparisonFunc (1..8)
    if (pIn->ComparisonFunc < 1 || pIn->ComparisonFunc > 8)
    {
        return SamplerTranslationStatus::InvalidComparisonFunc;
    }
    pOut->ComparisonFunc = static_cast<D3D12_COMPARISON_FUNC>(pIn->ComparisonFunc);

    // 9. Validate and set LODs
    if (std::isnan(pIn->MinLOD) || std::isinf(pIn->MinLOD) ||
        std::isnan(pIn->MaxLOD) || std::isinf(pIn->MaxLOD))
    {
        return SamplerTranslationStatus::NonFiniteFloat;
    }

    if (pIn->MinLOD < 0.0f)
    {
        return SamplerTranslationStatus::InvalidLODRange;
    }

    if (pIn->MinLOD > pIn->MaxLOD)
    {
        return SamplerTranslationStatus::InvalidLODRange;
    }

    // For MipMode=1 (indices 0 and 5): no mipmapping (mono-level) -> MaxLOD = MinLOD
    // For MipMode=2 (indices 1..4, 6..10): multi-level -> preserve MaxLOD
    pOut->MinLOD = pIn->MinLOD;
    if (entry.d2 == 1)
    {
        pOut->MaxLOD = pIn->MinLOD;
    }
    else
    {
        pOut->MaxLOD = pIn->MaxLOD;
    }

    // 10. BorderColor initialized to zero (BORDER address mode is rejected above)
    pOut->BorderColor[0] = 0.0f;
    pOut->BorderColor[1] = 0.0f;
    pOut->BorderColor[2] = 0.0f;
    pOut->BorderColor[3] = 0.0f;

    return SamplerTranslationStatus::Success;
}

// ============================================================================
// TranslateXboxSampler (Packed Header + Payload Coherence Check)
// ============================================================================
SamplerTranslationStatus TranslateXboxSampler(
    const XboxSamplerPackedHeader* pHeader,
    const XboxSamplerPayload* pPayload,
    D3D12_SAMPLER_DESC* pOut)
{
    if (!pHeader || !pPayload || !pOut)
    {
        return SamplerTranslationStatus::NullPointer;
    }

    // Validate header index ranges
    if (pHeader->filter_idx >= 11)
    {
        return SamplerTranslationStatus::InvalidFilterIndex;
    }
    if (pHeader->addr_u_idx >= 8 || pHeader->addr_v_idx >= 8 || pHeader->addr_w_idx >= 8)
    {
        return SamplerTranslationStatus::InvalidAddressModeIndex;
    }
    if (pHeader->comp_func_idx >= 8)
    {
        return SamplerTranslationStatus::InvalidComparisonFuncIndex;
    }

    // 1. Coherence: header.filter_idx vs MinFilter/MagFilter/MipFilter/MipMode
    const XboxFilterEntry& filterEntry = kXboxFilterTable[pHeader->filter_idx];
    if (pPayload->MinFilter != filterEntry.d0 ||
        pPayload->MagFilter != filterEntry.d0 ||
        pPayload->MipFilter != filterEntry.d1 ||
        pPayload->MipMode   != filterEntry.d2)
    {
        return SamplerTranslationStatus::HeaderPayloadFilterMismatch;
    }

    // 2. Coherence: header.addr_*_idx vs AddressU/V/W (lookup table check)
    if (pPayload->AddressU != kXboxAddressModeTable[pHeader->addr_u_idx] ||
        pPayload->AddressV != kXboxAddressModeTable[pHeader->addr_v_idx] ||
        pPayload->AddressW != kXboxAddressModeTable[pHeader->addr_w_idx])
    {
        return SamplerTranslationStatus::HeaderPayloadAddressMismatch;
    }

    // 3. Coherence: header.comp_func_idx vs ComparisonFunc
    if (pPayload->ComparisonFunc != kXboxComparisonTable[pHeader->comp_func_idx])
    {
        return SamplerTranslationStatus::HeaderPayloadComparisonMismatch;
    }

    // 4. Translate payload directly (no double lookup on addresses)
    return TranslateXboxSamplerDesc(pHeader->filter_idx, pPayload, pOut);
}

// ============================================================================
// TranslateXboxSamplerContainer (104 bytes)
// ============================================================================
SamplerTranslationStatus TranslateXboxSamplerContainer(
    const XboxSamplerContainer* pContainer,
    D3D12_SAMPLER_DESC* pOut)
{
    if (!pContainer || !pOut)
    {
        return SamplerTranslationStatus::NullPointer;
    }

    return TranslateXboxSampler(&pContainer->header, &pContainer->payload, pOut);
}

// ============================================================================
// TranslateXboxSamplerFromPayloadPointer (pDesc received in HookedCreateSamplerX)
// ============================================================================
// Caller Evidence:
// In game_release_xbox.exe at 0x1400A8CA0, the container is allocated as 104 bytes (0x68).
// Offset +0x00 is written with the 8-byte packed header: mov qword ptr [rcx], rax
// Offset +0x08 is the start of the 96-byte payload.
// At call site 0x1400B3C88: lea rdx, [rdi + 8]; call CreateSampler (slot 61).
// Therefore, pDesc passed to CreateSampler is guaranteed to be preceded by the 8-byte
// packed header at (pDesc - 8).
SamplerTranslationStatus TranslateXboxSamplerFromPayloadPointer(
    const void* pDesc,
    D3D12_SAMPLER_DESC* pOut)
{
    if (!pDesc || !pOut)
    {
        return SamplerTranslationStatus::NullPointer;
    }

    const uint8_t* pByte = static_cast<const uint8_t*>(pDesc);
    const XboxSamplerContainer* pContainer = reinterpret_cast<const XboxSamplerContainer*>(pByte - 8);

    return TranslateXboxSamplerContainer(pContainer, pOut);
}
