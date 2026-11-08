/**
 * @file MeshLODSettings.h
 * @brief 메시 LOD · 경계의 엔진 정책 값입니다(데이터 `engine/render/meshlod.xml`, 엔진 기본 에셋 `_meshLODSettings`).
 * @details 단계 바꾸기의 여유(`MeshLODSystem`)와 스킨드 메시의 경계 여유(`SkeletalMeshComponent`)를 C++ 상수가 아니라 데이터로 둔다 — 언리얼은 같은 값을
 *          콘솔 변수 · 에셋 설정(스켈레탈 메시 LOD Hysteresis · 컴포넌트 Bounds Scale)으로 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ResourceContext;
} // namespace sw

namespace sw
{
    /** @struct MeshLODSettings @brief 메시 LOD · 경계 정책 표입니다(엔진 기본 데이터 하나 — 처음 물을 때 읽는다). */
    REFLECT()
    struct SW_API MeshLODSettings
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Extra screen size (ratio) a mesh needs above a level's threshold before it goes back to the finer level - stops LOD flicker at the threshold" )
        float32 _refineHysteresis{ 0.0f };
        PROPERTY( Min = 1.0, Tooltip = "Skinned mesh bounds = bind-pose bounds x this - animation moves vertices outside the bind pose, culling must not clip them" )
        float32 _skinnedBoundsScale{ 0.0f };

        /** @brief XML 을 읽습니다. 모르는 키 · 범위 밖 값은 오류이고 false 입니다(`ConfigManager::readConfigXMLDocument`). */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 합친 글을 읽고 검사합니다(`ConfigManager::readComposedXMLDocument`). 리소스 id 로 읽으면 @p context 에 `withAsset` 이 실린다. */
        [[nodiscard]] bool loadComposed( string_view xml, string_view sourceLabel, const ResourceContext& context );
        /** @brief 엔진 기본 표(`EngineDefaultAssets::_meshLODSettings`)를 한 번 읽어 돌려줍니다. 읽지 못하면 오류로 멈춘다(값이 없으면 단계 · 컬링이 정해지지 않는다). */
        static const MeshLODSettings& getDefault();
    };
} // namespace sw
