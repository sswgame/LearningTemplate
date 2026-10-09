#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Shared/Api/EconomyMirror.h"

#include "Core/String/StringUtil.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct EconomyMirrorInternal
        {
            /** @brief @p assetId 가 @p prefix 로 시작하면 뒤 이름을 @p outName 에 넣고 true 입니다. */
            static bool findSuffix( string_view assetId, string_view prefix, hashed_string& outName )
            {
                if ( StringUtil::startsWith( assetId, prefix ) == false || assetId.size() == prefix.size() )
                    return false;
                outName = hashed_string( assetId.substr( prefix.size() ) );
                return true;
            }

            static bool isListed( const vector<LedgerBalance>& listBalance, string_view prefix, const hashed_string& name )
            {
                for ( const LedgerBalance& balance : listBalance )
                {
                    hashed_string listedName;
                    if ( findSuffix( balance._assetId, prefix, listedName ) && listedName == name )
                        return true;
                }
                return false;
            }

            static void setWalletBalance( Wallet& inoutWallet, const hashed_string& currency, int64 target )
            {
                const int64 current = inoutWallet.getBalance( currency );
                if ( target > current )
                    inoutWallet.add( currency, target - current );
                else if ( target < current )
                    inoutWallet.charge( currency, current - target ); // 빚(음수)까지 그대로 — 원장이 정본이다
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void EconomyMirror::applyToWallet( const vector<LedgerBalance>& listBalance, bool bSnapshot, Wallet& inoutWallet )
    {
        for ( const LedgerBalance& balance : listBalance )
        {
            hashed_string currency;
            if ( EconomyMirrorInternal::findSuffix( balance._assetId, kCurrencyPrefix, currency ) )
                EconomyMirrorInternal::setWalletBalance( inoutWallet, currency, balance._amount );
        }
        if ( bSnapshot == false )
            return;
        const vector<WalletBalance> listWallet = inoutWallet.getBalances(); // 아래에서 바꾸므로 복사
        for ( const WalletBalance& walletBalance : listWallet )
        {
            const bool bListed = EconomyMirrorInternal::isListed( listBalance, kCurrencyPrefix, walletBalance._currency );
            if ( bListed == false && walletBalance._amount != 0 )
                EconomyMirrorInternal::setWalletBalance( inoutWallet, walletBalance._currency, 0 );
        }
    }

    int32 EconomyMirror::applyToInventory( const vector<LedgerBalance>& listBalance, bool bSnapshot, Inventory& inoutInventory )
    {
        // 스냅숏이면 원장에 없는 아이템을 먼저 뺀다 — 빈 칸이 생긴 뒤에 넣어야 칸이 모자라지 않는다.
        if ( bSnapshot )
        {
            vector<hashed_string> listHeld;
            for ( int32 slot = 0; slot < inoutInventory.getSlotCount(); ++slot )
            {
                const InventorySlot& held = inoutInventory.getSlot( slot );
                if ( held.isEmpty() == false && std::find( listHeld.begin(), listHeld.end(), held._itemId ) == listHeld.end() )
                    listHeld.push_back( held._itemId );
            }
            for ( const hashed_string& itemId : listHeld )
            {
                if ( EconomyMirrorInternal::isListed( listBalance, kItemPrefix, itemId ) )
                    continue;
                const bool bRemoved = inoutInventory.removeItem( itemId, inoutInventory.getItemCount( itemId ) );
                SW_ASSERT( bRemoved ); // 가진 개수를 그대로 빼므로 늘 된다
                (void)bRemoved;
            }
        }
        int32 overflowCount = 0;
        for ( const LedgerBalance& balance : listBalance )
        {
            hashed_string itemId;
            if ( EconomyMirrorInternal::findSuffix( balance._assetId, kItemPrefix, itemId ) == false )
                continue;
            const int64 target  = balance._amount > 0 ? balance._amount : 0; // 아이템 빚은 0 으로 비춘다
            const int64 current = inoutInventory.getItemCount( itemId );
            if ( target > current )
            {
                const int32 wanted = static_cast<int32>( target - current );
                overflowCount += wanted - inoutInventory.addItem( itemId, wanted );
            }
            else if ( target < current )
            {
                const bool bRemoved = inoutInventory.removeItem( itemId, static_cast<int32>( current - target ) );
                SW_ASSERT( bRemoved ); // 가진 개수보다 적게 빼므로 늘 된다
                (void)bRemoved;
            }
        }
        return overflowCount;
    }
} // namespace sw
