/**
 * @file MeshFracture.h
 * @brief 닫힌 메시를 보로노이 칸으로 쪼갭니다 — 씨앗점 배치(고르게 · 맞은 자리 둘레 · 평면 조각), 칸마다 메시를 평면으로 잘라 안쪽 면을 막고,
 *        조각마다 볼록 껍질 · 무게 중심 · 부피, 조각 사이 맞닿은 넓이, 묶음 계층을 냅니다. 결과는 `FractureAsset` 입니다.
 * @details 칸 i 는 모든 j 에 대해 "i 쪽 이등분 평면의 안쪽" 의 교집합(볼록 다면체)입니다. 메시를 그 평면들로 차례로 자르면(가까운 씨앗부터,
 *          조각이 평면에 닿지 않으면 건너뛰고, 남은 평면이 조각 반경보다 멀면 끝) 조각 = 메시 ∩ 칸이 남습니다. 자른 자리마다 끊긴 고리를 이어
 *          귀 자르기로 막습니다(오목 · 구멍 난 단면도). 같은 모서리를 나누는 두 삼각형이 같은 교점을 비트까지 같게 얻도록 교점은 끝점을 정렬해 구합니다 —
 *          그래서 캡 고리가 정확히 닫히고 조각은 다시 닫힌 메시입니다. 막은 면은 "j 쪽 평면에서 생겼다" 를 기억해 두 조각의 맞닿은 넓이가 됩니다.
 *
 *          입력은 닫혀 있어야 합니다(`GeometryCutUtil::isClosed` — 같은 자리 정점을 하나로 보고 모든 모서리를 두 삼각형이 나눈다). 겹친 닫힌 부품
 *          여럿도 됩니다. 자르기 전에 허용 오차 안의 자리를 한 자리로 용접합니다. 조각은 자리가 비트까지 같은 정점끼리 닫혀 있습니다(`isClosedMesh`) —
 *          세 평면이 만나는 칸 꼭짓점 둘레에는 아주 짧은 변이 남습니다. 씨앗과 난수는 `DestructionRandom` 이라 같은 메시 · 설정 · 씨앗이면 어느 기계에서도 같은 조각입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct FractureAsset;

    /** @brief 씨앗점을 놓는 방식입니다. */
    ENUM()
    enum class FracturePattern : uint8
    {
        Uniform = 0, ///< 메시 안에 고르게
        Clustered,   ///< 맞은 자리(`_impactPoint`) 둘레에 몰리게 — 가까이는 잘게, 멀리는 크게
        Slices,      ///< 격자(`_arrSliceCount`)에 흔들림을 준 자리 — 한 축이면 평면 조각, 둘이면 벽돌
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 무엇을 부피로 보고 쪼갤지입니다. 게임 모델은 대개 닫혀 있지 않습니다(판자 · 겹친 부품 · 열린 바닥) — 그때는 닫힌 대리 부피를 쪼개
     *        안쪽 면 · 물리 껍질 · 부피를 얻고, 겉면은 원래 메시를 같은 평면으로 (막지 않고) 잘라 씁니다.
     */
    ENUM()
    enum class FractureVolume : uint8
    {
        Mesh = 0, ///< 메시 자체(닫혀 있어야 한다)
        Bounds,   ///< 메시의 경계 상자
        Hull,     ///< 메시 정점의 볼록 껍질
    };
} // namespace sw

namespace sw
{
    /** @brief 쪼개기 설정입니다(모델 임포트 규칙 `fracture` 가 채운다). */
    struct SW_API FractureSettings
    {
        vector<uint32>  _listLevelCount;  ///< 클러스터 레벨마다 클러스터 수(위 → 아래). 비면 뿌리 하나
        float4          _interiorColor;   ///< 안쪽 면 정점 색
        float3          _impactPoint;     ///< Clustered: 맞은 자리(메시 공간)
        float32         _clusterRadius;   ///< Clustered: 몰리는 반경(미터)
        float32         _clusterFraction; ///< Clustered: 반경 안에 놓을 씨앗의 몫(0..1)
        float32         _interiorUvScale; ///< 안쪽 면 평면 투영 UV 의 미터당 배율
        float32         _sliceJitter;     ///< Slices: 셀 크기에 대한 흔들림(0..0.5)
        uint64          _seed;
        uint32          _pieceCount;       ///< Uniform · Clustered: 조각 수(씨앗 수 — 메시 밖 칸은 빠진다)
        uint32          _arrSliceCount[3]; ///< Slices: 축마다 셀 수
        uint32          _maxHullPoint;     ///< 조각 껍질 점의 상한(넘으면 고른 방향의 끝점만)
        FracturePattern _pattern;
        FractureVolume  _volume; ///< 부피(닫히지 않은 모델은 Bounds · Hull)

        FractureSettings();

        /** @brief 이름("uniform" · "clustered" · "slices")을 읽습니다. 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool parsePattern( string_view text, FracturePattern& outPattern );
        /** @brief 이름입니다. */
        static string_view getPatternName( FracturePattern pattern );
        /** @brief 이름("mesh" · "bounds" · "hull")을 읽습니다. 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool parseVolume( string_view text, FractureVolume& outVolume );
        /** @brief 이름입니다. */
        static string_view getVolumeName( FractureVolume volume );
    };
} // namespace sw

namespace sw
{
    /** @brief 메시 쪼개기 함수 모음입니다(전부 static). */
    struct SW_API MeshFractureUtil
    {
        /** @brief 쪼개기 결과를 바꾸는 고침마다 올립니다 — 임포트 원본 해시에 섞여 쿠킹한 `.fracture` 가 어긋남이 된다(형식 버전과 따로). */
        static constexpr uint32 kAlgorithmVersion = 1;
        /**
         * @brief 인덱스 없는 삼각형 목록을 쪼개 @p outAsset 을 채웁니다.
         * @details `_volume` 이 Mesh 가 아니면 대리 부피(경계 상자 · 볼록 껍질)를 쪼개 안쪽 면 · 껍질 · 부피를 얻고, 겉면은 원래 메시를 같은 평면으로
         *          막지 않고 잘라 조각마다 붙입니다 — 열린 판자 · 겹친 부품으로 된 모델(Kenney 상자)도 쪼갭니다.
         * @return 입력이 (Mesh 부피인데) 닫혀 있지 않거나 삼각형이 없거나 조각이 하나도 남지 않으면 false 이고 @p outError 에 까닭입니다.
         */
        [[nodiscard]] static bool fracture( vector_reference<const RHIVertex> listVertex, const FractureSettings& settings, FractureAsset& outAsset, string& outError );
        /** @brief 삼각형 목록이 닫혔는지입니다 — 자리가 비트까지 같은 점만 같은 정점으로 봅니다(쪼갠 조각이 정확히 닫혔는지). */
        static bool isClosedMesh( vector_reference<const RHIVertex> listVertex );
        /** @brief 허용 오차(1e-5) 안의 자리를 같은 정점으로 보고 닫혔는지입니다(`GeometryCutUtil::isClosed` — 임포트한 입력 검사). */
        static bool isClosedWithinTolerance( vector_reference<const RHIVertex> listVertex );
        /** @brief 닫힌 삼각형 목록의 부피(감은 방향 무관 절댓값)와 무게 중심입니다. */
        static float32 computeVolume( vector_reference<const RHIVertex> listVertex, float3& outCentroid );
        /** @brief 정점들의 볼록 껍질(닫힌 삼각형 목록, 바깥 반시계)입니다. 점이 한 평면에 있으면 비어 있습니다. */
        static void makeConvexHull( vector_reference<const RHIVertex> listVertex, vector<RHIVertex>& outListVertex );
        /** @brief 점이 닫힌 삼각형 목록 안에 있는지입니다(감음 수 — 구면각 합). */
        static bool isPointInside( vector_reference<const RHIVertex> listVertex, const float3& point );
    };
} // namespace sw
