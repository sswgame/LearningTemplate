/**
 * @file BrLoot.h
 * @brief 루팅을 다룹니다. 건물과 지점 종류마다의 전리품 테이블(기반 `LootCatalog`)로 맵 전체의 아이템 배치를 시드로 만들고, 보급 상자의 내용물을 굴립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BrCatalog;
    class GameRandom;
    class LootCatalog;

    /** @brief 맵의 전리품 지점 하나(건물 방, 창고 선반)입니다. */
    struct BrLootSpot
    {
        float2        _position{};
        hashed_string _kind{}; ///< `BrLootSpotDef` id
    };
} // namespace sw

namespace sw
{
    /** @brief 바닥에 놓인 아이템 한 무더기입니다. */
    struct BrGroundItem
    {
        float2        _position{};
        hashed_string _itemID{};
        int32         _count{ 0 };
        int32         _spotIndex{ -1 }; ///< 나온 지점(보급 상자면 −1)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct BrLootPlacement
     * @brief 배치를 만드는 규칙입니다. 지점마다 난수를 (지점 번호, 시드) 해시로 따로 만들므로, 지점 하나를 더하거나 빼도 다른 지점의 아이템은 그대로입니다.
     * @details 지점의 확률(`_chance`)을 넘으면 `_minRolls`..`_maxRolls` 번 테이블을 굴리고, 같은 아이템은 합쳐서 id 사전순으로 놓는다(결정적이다).
     */
    struct SW_KIT_API BrLootPlacement
    {
        /** @brief 맵 전체 배치를 @p outListItem 에 채웁니다(앞의 내용은 지웁니다). 모르는 지점 종류는 건너뛰며, 놓은 무더기 수를 반환합니다. */
        static int32 placeMapLoot( const BrCatalog& catalog, const LootCatalog& lootCatalog, const vector<BrLootSpot>& listSpot, uint32 seed,
                                   vector<BrGroundItem>& outListItem );
        /** @brief 테이블 하나를 굴려 @p position 에 놓을 무더기를 @p outListItem 뒤에 붙입니다(보급 상자). 붙인 수를 반환합니다. */
        static int32 rollTableAt( const LootCatalog& lootCatalog, const hashed_string& tableID, const float2& position, int32 spotIndex, int32 rollCount,
                                  GameRandom& random, vector<BrGroundItem>& outListItem );
    };
} // namespace sw
