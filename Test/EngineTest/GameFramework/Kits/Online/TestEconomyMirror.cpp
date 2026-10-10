// 경제 거울 — 원장 잔액 → 지갑(사건 · 빚 포함) · 인벤토리(차이만큼, 빚은 0), 스냅숏은 없는 것을 0 으로(먼저 빼고 넣는다).
#include "pch.h"

#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/API/EconomyMirror.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( EconomyMirrorTest, WalletFollowsLedgerWithEvents )
{
    Wallet wallet;
    wallet.add( "Gold", 50 );
    vector<WalletEvent> listEvent;
    wallet.drainEvents( listEvent );
    listEvent.clear();
    EconomyMirror::applyToWallet( {
                                      LedgerBalance{   "cur.gold", 70, 0},
                                      LedgerBalance{"item.potion",  3, 0}
    },
                                  false, wallet );
    SW_EXPECT_EQUAL( wallet.getBalance( "Gold" ), int64( 70 ) ); // hashed_string 은 대소문자를 무시한다
    wallet.drainEvents( listEvent );
    SW_ASSERT_EQUAL( listEvent.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listEvent[0]._delta, int64( 20 ) );
}

SW_TEST_CASE( EconomyMirrorTest, WalletSnapshotZeroesUnlistedAndMirrorsDebt )
{
    Wallet wallet;
    wallet.add( "gold", 10 );
    wallet.add( "gem_free", 5 );
    wallet.add( "gem_paid", 5 );
    EconomyMirror::applyToWallet( {
                                      LedgerBalance{    "cur.gold",  10, 0},
                                      LedgerBalance{"cur.gem_paid", -30, 0}
    },
                                  true, wallet );
    SW_EXPECT_EQUAL( wallet.getBalance( "gold" ), int64( 10 ) );
    SW_EXPECT_EQUAL( wallet.getBalance( "gem_free" ), int64( 0 ) );
    SW_EXPECT_EQUAL( wallet.getBalance( "gem_paid" ), int64( -30 ) ); // 환불 회수의 빚 — 원장 그대로
    SW_EXPECT_FALSE( wallet.trySpend( "gem_paid", 1 ) );              // 빚이 있는 동안 쓰지 못한다
}

SW_TEST_CASE( EconomyMirrorTest, InventoryCountsFollowLedgerAndReportOverflow )
{
    ItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromXMLText( R"(<ItemCatalog><Item id="potion" maxStack="10"/><Item id="sword"/></ItemCatalog>)", "items" ) );
    Inventory inventory;
    inventory.initialize( &items, 2 );
    SW_ASSERT_EQUAL( inventory.addItem( "sword", 1 ), 1 );
    const int32 overflow = EconomyMirror::applyToInventory( {
                                                                LedgerBalance{ "item.potion", 15, 0 }
    },
                                                            true, inventory );
    SW_EXPECT_EQUAL( inventory.getItemCount( "sword" ), 0 );   // 스냅숏에 없다 — 원장에 없는 칼은 거울에서도 없다
    SW_EXPECT_EQUAL( inventory.getItemCount( "potion" ), 15 ); // 칸 둘 × 10
    SW_EXPECT_EQUAL( overflow, 0 );
    const int32 overflowMore = EconomyMirror::applyToInventory( {
                                                                    LedgerBalance{ "item.potion", 25, 0 }
    },
                                                                false, inventory );
    SW_EXPECT_EQUAL( overflowMore, 5 );
    // 넘친 개수는 0 — 아이템 빚 처리는 아래 개수 단언이 본다
    (void)EconomyMirror::applyToInventory( {
                                               LedgerBalance{ "item.potion", -2, 0 }
    },
                                           false, inventory );
    SW_EXPECT_EQUAL( inventory.getItemCount( "potion" ), 0 ); // 아이템 빚은 0 으로
}
