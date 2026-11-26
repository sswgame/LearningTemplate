/**
 * @file Engine/Graphics/Shader/ShaderSourceAssetKind.h
 * @brief 셰이더 원본(`*.hlsl`)을 가리키는 참조(`AssetRef<ShaderSourceAsset>`)의 대상 종류와, 원본이 include 로만 읽는 조각(`*.hlsli`)의 종류입니다.
 * @details 접미사 표만 있는 헤더라 1 층(팩 설정 · 기본 에셋 표)도 include 한다(`Config/Rules/Layers/EngineLayers.toml` 의 파일 키).
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
        /** @brief 원본 파일의 접미사입니다(에디터 슬롯의 거르개도 이 글을 쓴다). */
        static constexpr const utf8* kFileSuffix       = ".hlsl";
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kFileSuffix, AssetSuffixRole::Plain },
        };
        static constexpr AssetTypeDef kAssetType{ "ShaderSource", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief @p path 가 셰이더 원본이나 그 include 조각이면 true 입니다 — 원본을 빼는 팩(`PackConfig::_bExcludeRawShaderSource`)에는 둘 다 없고 쿠킹한 바이너리만 있다. */
        [[nodiscard]] static bool isRawSource( string_view path );
    };

    /**
     * @struct ShaderIncludeAsset
     * @brief 셰이더 원본이 `#include` 로만 읽는 조각의 종류입니다. 원본 종류와 따로 두어 `AssetRef<ShaderSourceAsset>` 이 조각을 받지 않는다(Godot `ShaderInclude` 가 `Shader` 와 따로인 자리).
     *        원본처럼 표에 올리지 않는다 — 바이너리는 `.hlsl` 마다 나오고 조각은 쿠킹 입력일 뿐이다.
     */
    struct ShaderIncludeAsset
    {
        static constexpr const utf8* kFileSuffix       = ".hlsli";
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kFileSuffix, AssetSuffixRole::Plain },
        };
        static constexpr AssetTypeDef kAssetType{ "ShaderInclude", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };
    };

    inline bool ShaderSourceAsset::isRawSource( string_view path )
    {
        return kAssetType.findSuffix( path ) != nullptr || ShaderIncludeAsset::kAssetType.findSuffix( path ) != nullptr;
    }
} // namespace sw
