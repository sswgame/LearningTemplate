/**
 * @file FractureSettings.h
 * @brief 쪼개기 설정(시드 배치 방식 · 부피 · 조각 수)입니다. 런타임 2D 쪼개기(`PolygonFractureUtil`, `Fracture2DComponent`)와 에디터의 3D 모델 임포트 쪼개기(`MeshFractureUtil`)가 함께 씁니다.
 * @details 3D 메시 쪼개기 알고리즘은 모델 임포트만 부르므로 에디터(`Editor/Common/Asset/Model/MeshFracture.h`)에 있다 — 언리얼도 쪼개기는 편집기(Fracture Mode)가,
 *          결과(Geometry Collection)는 런타임이 든다. 런타임은 구운 결과 `FractureAsset` 만 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 씨앗점을 놓는 방식입니다. */
    ENUM()
    enum class FracturePattern : uint8
    {
        Uniform = 0, ///< 메시 안에 고르게 놓는다.
        Clustered,   ///< 맞은 지점(`_impactPoint`) 둘레에 몰리게 놓는다. 가까이는 잘게, 멀리는 크게 쪼갠다.
        Slices,      ///< 격자(`_arrSliceCount`)에 흔들림을 준 위치에 놓는다. 한 축이면 평면 조각, 두 축이면 벽돌이 된다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 무엇을 부피로 보고 쪼갤지 정합니다. 게임 모델은 대개 닫혀 있지 않습니다(판자, 겹친 부품, 열린 바닥).
     *        그럴 때는 닫힌 대리 부피를 쪼개 안쪽 면, 물리 껍질, 부피를 얻고, 겉면은 원래 메시를 같은 평면으로 (막지 않고) 잘라 씁니다.
     */
    ENUM()
    enum class FractureVolume : uint8
    {
        Mesh = 0, ///< 메시 자체다(닫혀 있어야 한다).
        Bounds,   ///< 메시의 경계 상자다.
        Hull,     ///< 메시 정점의 볼록 껍질이다.
    };
} // namespace sw

namespace sw
{
    /** @brief 쪼개기 설정입니다. 모델 임포트 규칙 `fracture` 와 `Fracture2DComponent` 가 채웁니다. */
    struct SW_API FractureSettings
    {
        vector<uint32>  _listLevelCount;  ///< 클러스터 레벨마다 클러스터 수다(위에서 아래로). 비면 루트 하나다.
        float4          _interiorColor;   ///< 안쪽 면의 정점 색이다.
        float3          _impactPoint;     ///< Clustered 에서 맞은 지점이다(메시 공간).
        float32         _clusterRadius;   ///< Clustered 에서 시드가 몰리는 반경이다(미터).
        float32         _clusterFraction; ///< Clustered 에서 반경 안에 놓을 시드의 비율이다(0..1).
        float32         _interiorUvScale; ///< 안쪽 면 평면 투영 UV 의 미터당 배율이다.
        float32         _sliceJitter;     ///< Slices 에서 셀 크기에 대한 흔들림이다(0..0.5).
        uint64          _seed;
        uint32          _pieceCount;       ///< Uniform, Clustered 에서 조각 수다(시드 수이며, 메시 밖 셀은 빠진다).
        uint32          _arrSliceCount[3]; ///< Slices 에서 축마다 셀 수다.
        uint32          _maxHullPoint;     ///< 조각 껍질 점의 상한이다. 넘으면 고른 방향의 끝점만 남긴다.
        FracturePattern _pattern;
        FractureVolume  _volume; ///< 부피다(닫히지 않은 모델은 Bounds 나 Hull 을 쓴다).

        FractureSettings();

        /** @brief 이름("uniform", "clustered", "slices")을 읽습니다. 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool parsePattern( string_view text, FracturePattern& outPattern );
        /** @brief 이름을 반환합니다. */
        static string_view getPatternName( FracturePattern pattern );
        /** @brief 이름("mesh", "bounds", "hull")을 읽습니다. 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool parseVolume( string_view text, FractureVolume& outVolume );
        /** @brief 이름을 반환합니다. */
        static string_view getVolumeName( FractureVolume volume );
    };
} // namespace sw
