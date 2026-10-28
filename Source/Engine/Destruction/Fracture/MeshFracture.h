/**
 * @file MeshFracture.h
 * @brief 닫힌 메시를 보로노이 셀로 쪼갭니다. 시드 점을 배치하고(고르게, 맞은 지점 둘레, 평면 조각), 셀마다 메시를 평면으로 잘라 안쪽 면을 막습니다.
 *        조각마다 볼록 껍질, 무게 중심, 부피, 조각 사이 맞닿은 넓이, 묶음 계층을 구합니다. 결과는 `FractureAsset` 입니다.
 * @details 셀 i 는 모든 j 에 대해 "i 쪽 이등분 평면의 안쪽"의 교집합(볼록 다면체)이다.
 *          메시를 그 평면들로 차례로 자르면 조각 = 메시 ∩ 셀이 남는다. 가까운 시드부터 자르고, 조각이 평면에 닿지 않으면 건너뛰며, 남은 평면이 조각 반경보다 멀면 끝낸다.
 *          자른 곳마다 끊긴 고리를 이어 귀 자르기로 막는다(오목하거나 구멍 난 단면도 처리한다).
 *          같은 모서리를 나누는 두 삼각형이 같은 교점을 비트까지 같게 얻도록, 교점은 끝점을 정렬해서 구한다.
 *          그래서 캡 고리가 정확히 닫히고 조각은 다시 닫힌 메시가 된다.
 *          막은 면은 "j 쪽 평면에서 생겼다"를 기억해 두므로, 두 조각의 맞닿은 넓이를 구할 수 있다.
 *
 *          입력은 닫혀 있어야 한다. `GeometryCutUtil::isClosed` 는 같은 위치의 정점을 하나로 보고, 모든 모서리를 두 삼각형이 나누는지 검사한다.
 *          겹친 닫힌 부품 여럿도 입력으로 쓸 수 있다. 자르기 전에 허용 오차 안의 위치를 한 점으로 용접한다.
 *          조각은 위치가 비트까지 같은 정점끼리 닫혀 있다(`isClosedMesh`).
 *          세 평면이 만나는 셀 꼭짓점 둘레에는 아주 짧은 변이 남는다.
 *          시드와 난수는 `DestructionRandom` 이 만들므로, 같은 메시와 설정과 시드를 주면 어느 기계에서도 같은 조각이 나온다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
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
    /** @brief 쪼개기 설정입니다. 모델 임포트 규칙 `fracture` 가 채웁니다. */
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

namespace sw
{
    /** @brief 메시 쪼개기 함수 모음입니다. 전부 static 입니다. */
    struct SW_API MeshFractureUtil
    {
        /** @brief 쪼개기 결과를 바꾸는 수정마다 올립니다. 임포트 원본 해시에 섞여, 쿠킹해 둔 `.fracture` 가 낡은 것으로 판정됩니다(형식 버전과는 별개입니다). */
        static constexpr uint32 kAlgorithmVersion = 1;
        /**
         * @brief 인덱스 없는 삼각형 목록을 쪼개 @p outAsset 을 채웁니다.
         * @details `_volume` 이 Mesh 가 아니면 대리 부피(경계 상자나 볼록 껍질)를 쪼개 안쪽 면, 껍질, 부피를 얻고,
         *          겉면은 원래 메시를 같은 평면으로 막지 않고 잘라 조각마다 붙인다. 열린 판자나 겹친 부품으로 된 모델(Kenney 상자)도 쪼갠다.
         * @return 입력이 (Mesh 부피인데) 닫혀 있지 않거나 삼각형이 없거나 조각이 하나도 남지 않으면 false 를 반환하고, @p outError 에 이유를 씁니다.
         */
        [[nodiscard]] static bool fracture( vector_reference<const RHIVertex> listVertex, const FractureSettings& settings, FractureAsset& outAsset, string& outError );
        /** @brief 삼각형 목록이 닫혔는지 반환합니다. 위치가 비트까지 같은 점만 같은 정점으로 봅니다(쪼갠 조각이 정확히 닫혔는지 확인할 때 씁니다). */
        static bool isClosedMesh( vector_reference<const RHIVertex> listVertex );
        /** @brief 허용 오차(1e-5) 안의 위치를 같은 정점으로 보고 닫혔는지 반환합니다. `GeometryCutUtil::isClosed` 를 쓰며, 임포트한 입력을 검사합니다. */
        static bool isClosedWithinTolerance( vector_reference<const RHIVertex> listVertex );
        /** @brief 닫힌 삼각형 목록의 부피(감은 방향과 무관한 절댓값)와 무게 중심을 반환합니다. */
        static float32 computeVolume( vector_reference<const RHIVertex> listVertex, float3& outCentroid );
        /** @brief 정점들의 볼록 껍질(닫힌 삼각형 목록, 바깥이 반시계)을 반환합니다. 점이 한 평면에 있으면 비어 있습니다. */
        static void makeConvexHull( vector_reference<const RHIVertex> listVertex, vector<RHIVertex>& outListVertex );
        /** @brief 점이 닫힌 삼각형 목록 안에 있는지 반환합니다(감음 수, 구면각 합). */
        static bool isPointInside( vector_reference<const RHIVertex> listVertex, const float3& point );
    };
} // namespace sw
