/**
 * @file TradeInventoryUtil.h
 * @brief 거래 화면이 인벤토리 칸에서 다리를 만듭니다 — 칸 → (원장 자산 id, 수량). 같은 아이템 칸은 한 다리로 합친다.
 * @details 아이템 id → 자산 id 는 게임이 이어 준다(`AssetIdFromItem` — 경제 키트의 카탈로그가 뜻을 준다, 키트끼리는 모른다).
 *          인스턴스 상태가 있는 칸(꾸미기 · 떨어진 부품 · 외형 피해 · 내구도)은 `NotTradable` — 원장 1 판은 개수 자산만 다룬다(인스턴스 아이템은 백로그).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/TradeTypes.h"

namespace sw
{
    class Inventory;

    /** @brief 아이템 id 를 원장 자산 id 로 바꿉니다. 거래할 수 없는 아이템이면 false. */
    using AssetIdFromItem = bool ( * )( const hashed_string& itemId, string& outAssetId );

    /** @brief 인벤토리 → 거래 다리 도우미입니다. */
    struct SW_GF_API TradeInventoryUtil
    {
        /** @brief @p listSlot 칸들로 다리를 만듭니다. 빈 칸 · 범위 밖 칸은 Invalid, 인스턴스 상태 · 바꿀 수 없는 아이템은 NotTradable. */
        static TradeResult makeLegs( const Inventory& inventory, const vector<int32>& listSlot, AssetIdFromItem pAssetIdFromItem, vector<TradeLeg>& outListLeg );
    };
} // namespace sw
