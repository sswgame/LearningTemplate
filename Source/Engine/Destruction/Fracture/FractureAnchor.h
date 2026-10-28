/**
 * @file FractureAnchor.h
 * @brief 어느 잎을 앵커(땅에 붙은 조각)로 볼지 정하는 순수 계산입니다. `FractureComponentBase` 가 상태를 시작할 때 한 번 부릅니다.
 * @details 물리를 모른다. `World` 모드의 "정적 월드에 닿았나" 는 물리 질의라 부르는 쪽이 잎마다 물어 `listWorldContact` 로 넘긴다.
 *          그래서 엔진이나 물리 씬 없이 쿠킹한 `FractureAsset` 하나로 테스트할 수 있다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    enum class FractureAnchorMode : uint8;

    struct FractureAnchorVolume;
    struct FractureAsset;

    /** @brief 앵커 판정 도우미입니다. 전부 static 입니다. */
    struct SW_API FractureAnchorUtil
    {
        /** @brief 잎이 가장 낮은 면에서 @p tolerance(월드 미터) 안에 닿는지 반환합니다(`Bottom` 모드). @p scale 은 오브젝트의 고른 배율입니다. */
        static bool isBottomLeaf( const FractureAsset& asset, uint32 leaf, float32 tolerance, float32 scale );
        /** @brief 메시 공간 점이 앵커 볼륨(오브젝트 로컬 상자) 하나라도 안에 있는지 반환합니다. 경계는 안으로 칩니다. */
        static bool isInsideAnyVolume( const float3& point, vector_reference<const FractureAnchorVolume> listVolume );
        /**
         * @brief 잎마다 앵커 여부(1 = 앵커)를 정합니다. 모드의 판정에 앵커 볼륨(무게 중심 기준)을 더합니다.
         * @param listWorldContact `World` 모드에서 잎마다 다른 오브젝트의 정적 바디에 닿았는지입니다. 다른 모드이거나 물을 수 없으면 비워 둡니다(그러면 닿지 않은 것으로 봅니다).
         */
        static void computeAnchors( const FractureAsset& asset, FractureAnchorMode mode, float32 tolerance, float32 scale, vector_reference<const FractureAnchorVolume> listVolume,
                                    vector_reference<const uint8> listWorldContact, vector<uint8>& outListAnchor );
    };
} // namespace sw
