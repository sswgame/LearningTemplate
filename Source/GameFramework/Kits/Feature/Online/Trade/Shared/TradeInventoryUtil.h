/**
 * @file TradeInventoryUtil.h
 * @brief 거래 화면이 인벤토리 슬롯에서 거래 항목을 만듭니다. 슬롯을 (원장 자산 id, 수량)으로 바꾸며 같은 아이템의 슬롯은 한 항목으로 합칩니다.
 * @details 아이템 id 를 자산 id 로 바꾸는 일은 게임이 연결해 준다(`AssetIDFromItem`). 경제 키트의 카탈로그가 그 뜻을 주며 키트끼리는 서로 모른다.
 *          인스턴스 상태가 있는 슬롯(꾸미기, 떨어진 부품, 외형 피해, 내구도)은 `NotTradable` 이다. 원장 1차 버전은 개수 자산만 다루기 때문이다. 인스턴스 아이템은 아직 지원하지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeTypes.h"

namespace sw
{
    class Inventory;

    /** @brief 아이템 id 를 원장 자산 id 로 바꿉니다. 거래할 수 없는 아이템이면 false 를 반환합니다. */
    using AssetIDFromItem = bool ( * )( const hashed_string& itemID, string& outAssetID );

    /** @brief 인벤토리에서 거래 항목을 만드는 보조 함수 모음입니다. */
    struct SW_KIT_API TradeInventoryUtil
    {
        /** @brief @p listSlot 슬롯들로 거래 항목을 만듭니다. 빈 슬롯이거나 범위 밖 슬롯이면 Invalid 이고, 인스턴스 상태가 있거나 바꿀 수 없는 아이템이면 NotTradable 입니다. */
        static TradeResult makeLegs( const Inventory& inventory, const vector<int32>& listSlot, AssetIDFromItem pAssetIDFromItem, vector<TradeLeg>& outListLeg );
    };
} // namespace sw
