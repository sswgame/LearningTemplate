/**
 * @file FarmShippingBin.h
 * @brief 출하함을 구현합니다. 넣은 것은 하루가 끝날 때 팔립니다(하베스트 문의 출하 상자). 가방과 지갑은 빌려 씁니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class CropCatalog;
    class Inventory;
    class Wallet;

    /**
     * @class FarmShippingBin
     * @brief 출하함입니다. 넣은 것은 하루 끝에 카탈로그 가격으로 팔립니다. 가방과 지갑은 빌려 씁니다(`Inventory&`, `Wallet&`).
     * @details 담긴 것은 "팔릴 목록" 이라서 `ItemStackList`(값 목록)로 보관한다. 슬롯, 무게, 꾸미기는 없다.
     */
    class SW_GF_API FarmShippingBin
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "FSHP" );
        static constexpr uint32 kStateVersion = 1;

        FarmShippingBin();

        void                 setCurrency( const hashed_string& currency ) { _currency = currency; }
        const hashed_string& getCurrency() const { return _currency; }
        /** @brief @p inoutBag 에서 꺼내 출하함에 넣습니다. 가방에 모자라면 아무것도 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool shipItem( Inventory& inoutBag, const hashed_string& itemID, int32 count );
        int32              getShippedItemCount() const { return _bin.getTotalCount(); }
        /** @brief 하루 끝에 정산합니다. 카탈로그 가격으로 팔아 @p inoutWallet 에 더하고 비운 뒤, 번 돈을 반환합니다(가격을 모르는 아이템은 0 으로 팝니다). */
        int32 settleShipping( const CropCatalog& catalog, Wallet& inoutWallet );

        /** @brief 출하함에 든 것을 씁니다(핫 리로드와 세이브용). 통화는 설정이므로 저장하지 않습니다. */
        void writeState( Archive& outArchive ) const { _bin.writeState( outArchive ); }
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        ItemStackList _bin;
        hashed_string _currency;
    };
} // namespace sw
