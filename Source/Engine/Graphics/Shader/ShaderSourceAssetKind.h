/**
 * @file Engine/Graphics/Shader/ShaderSourceAssetKind.h
 * @brief 셰이더 원본(`*.hlsl`)을 가리키는 참조(`AssetRef<ShaderSourceAsset>`)의 대상 종류입니다.
 */
#pragma once
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @struct ShaderSourceAsset
     * @brief 셰이더 원본의 종류입니다. 원본은 배포본에 들지 않으므로(쿠킹본 `ShaderBinary` 가 간다) 표에 올리지 않고 타입 있는 참조의 검사에만 쓴다(`AudioPlayableAsset` 과 같다).
     *        배포본의 팩 색인은 닿은 원본의 줄을 싣는다(`PackCooker` 의 `_listReferencedExcluded`) — GUID 참조가 그대로 풀린다.
     */
    struct ShaderSourceAsset
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".hlsl", AssetSuffixRole::Plain },
        };
        static constexpr AssetTypeDef kAssetType{ "ShaderSource", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };
    };
} // namespace sw
