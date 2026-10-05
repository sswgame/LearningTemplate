/**
 * @file FarmShippingBin.h
 * @brief 출하함입니다 — 넣은 것은 하루가 끝날 때 팔립니다(하베스트 문의 출하 상자). 가방 · 지갑은 빌립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Inventory/ItemStackList.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class CropCatalog;
    class Inventory;
    class Wallet;

    /**
     * @class FarmShippingBin
     * @brief 출하함 — 넣은 것은 하루 끝에 카탈로그 값으로 팔린다(하베스트 문의 출하 상자). 가방 · 지갑은 빌린다(`Inventory&` · `Wallet&`).
     * @details 담긴 것은 "팔릴 목록" 이라 `ItemStackList`(값 목록)이다 — 칸 · 무게 · 꾸미기가 없다.
     */
    class SW_GF_API FarmShippingBin
    {
    public:
        static constexpr uint32 kStateTag     = 0x50485346u; ///< 'FSHP'
        static constexpr uint32 kStateVersion = 1;

        FarmShippingBin();

        void                 setCurrency( const hashed_string& currency ) { _currency = currency; }
        const hashed_string& getCurrency() const { return _currency; }
        /** @brief @p inoutBag 에서 꺼내 출하함에 넣습니다. 가방에 모자라면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool shipItem( Inventory& inoutBag, const hashed_string& itemId, int32 count );
        int32              getShippedItemCount() const { return _bin.getTotalCount(); }
        /** @brief 하루 끝 정산 — 카탈로그 값으로 팔아 @p inoutWallet 에 더하고 비웁니다. 번 돈입니다(값을 모르는 아이템은 0 으로 팔린다). */
        int32 settleShipping( const CropCatalog& catalog, Wallet& inoutWallet );

        /** @brief 출하함에 든 것을 씁니다(핫 리로드 · 세이브). 통화는 설정이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const { _bin.writeState( outArchive ); }
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        ItemStackList _bin;
        hashed_string _currency;
    };
} // namespace sw
