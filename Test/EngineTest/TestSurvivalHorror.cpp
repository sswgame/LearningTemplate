// 생존 공포 · 조사 키트 — 카탈로그, 격자 가방(크기 · 회전 · 빈자리 · 겹침), 아이템 상자와 조합(자리가 없으면 되돌림), 세이브 제한,
// 정신력(어둠 · 괴물 목격 · 환각 · 조준 흔들림)과 손전등 배터리, 열쇠 문 · 다이얼 · 순서 퍼즐, 단서 보드 추리, 턴제 초자연 전투의 결정성.
#include "pch.h"

#include "GameFramework/Inventory/GridInventory.h"
#include "GameFramework/Kits/Horror/SurvivalHorror/HorrorCatalog.h"
#include "GameFramework/Kits/Horror/SurvivalHorror/HorrorEncounter.h"
#include "GameFramework/Kits/Horror/SurvivalHorror/HorrorSession.h"
#include "GameFramework/World/AreaGraph.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kHorrorTestXml = R"(
<HorrorCatalog>
  <Rules saveMode="InkRibbon" gridWidth="4" gridHeight="2" maxSanity="100" darknessDrain="10" sanityRegen="5" sanityRegenDelay="2"
         hallucinationThreshold="0.3" maxAimSway="2" maxBattery="20" batteryDrain="2" repeatSightingScale="0.25"/>
  <Item id="herbGreen" kind="Heal" heal="25"/>
  <Item id="herbRed" kind="Misc"/>
  <Item id="herbMix" kind="Heal" heal="100"/>
  <Item id="herbSuper" kind="Heal" w="2" h="2" heal="100"/>
  <Item id="sedative" kind="Heal" sanity="20"/>
  <Item id="battery" kind="Battery" battery="50"/>
  <Item id="ribbon" kind="SaveItem" maxStack="3"/>
  <Item id="ammo" kind="Ammo" maxStack="10"/>
  <Item id="rifle" kind="Weapon" w="3" h="1"/>
  <Item id="handgun" kind="Weapon" w="2" h="1"/>
  <Item id="case" kind="Misc" w="2" h="2"/>
  <Item id="junk" kind="Misc"/>
  <Item id="keyRed" kind="Key"/>
  <Item id="oddThing" kind="Spaceship"/>
  <Combine id="mixGR" a="herbGreen" b="herbRed" out="herbMix"/>
  <Combine id="mixSuper" a="herbMix" b="herbGreen" out="herbSuper"/>
  <Monster id="zombie" sanityLoss="30" toughness="4" damage="1" horror="1"/>
  <Monster id="elder" sanityLoss="40"/>
  <KeyLock id="redDoor" key="keyRed" flag="redDoorOpen"/>
  <DialLock id="safe" code="3 1 4" flag="safeOpen" attempts="3"/>
  <DialLock id="locker" code="9 9" flag="lockerOpen" attempts="2"/>
  <Sequence id="bells" steps="low,high,mid" flag="bellsDone" mistakeSanity="5"/>
  <Document id="diary" title="Diary" clues="knife,gloves"/>
  <Document id="note" clues="pantry"/>
  <Deduction id="culprit" answer="butler" links="knife-gloves,gloves-pantry" flag="caseSolved" wrongSanity="15"/>
</HorrorCatalog>
)";

    constexpr const utf8* kHorrorAreaXml = R"(
<AreaGraph>
  <Area id="hall" region="Mansion"/>
  <Area id="dining" region="Mansion"/>
  <Area id="study" region="Mansion"/>
  <Area id="vault" region="Mansion"/>
  <Link from="hall" to="dining"/>
  <Link from="hall" to="study" requires="redDoorOpen"/>
  <Link from="study" to="vault" requires="safeOpen &amp;&amp; bellsDone"/>
</AreaGraph>
)";

    bool hasEvent( const vector<HorrorEvent>& listEvent, HorrorEvent::Kind kind )
    {
        for ( const HorrorEvent& event : listEvent )
        {
            if ( event._kind == kind )
                return true;
        }
        return false;
    }
} // namespace

SW_TEST_CASE( SurvivalHorrorTest, CatalogReadsItemsCombinesPuzzlesAndRules )
{
    HorrorCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kHorrorTestXml, "SurvivalHorrorTest" ) );
    const HorrorItemDef* pRifle = catalog.findItem( "rifle" );
    SW_ASSERT_NOT_NULL( pRifle );
    SW_EXPECT_EQUAL( 3, pRifle->_width );
    SW_EXPECT_EQUAL( 1, pRifle->_height );
    SW_EXPECT_TRUE( pRifle->_kind == HorrorItemKind::Weapon );
    SW_EXPECT_EQUAL( 10, catalog.findItem( "ammo" )->_maxStack );
    SW_EXPECT_TRUE( catalog.findItem( "oddThing" )->_kind == HorrorItemKind::Misc ); // 모르는 종류는 경고하고 기본값

    // 조합은 순서가 없다 — 기반 RecipeCatalog 의 Combine 작업대 레시피.
    const RecipeDef* pMix = catalog.findCombine( "herbRed", "herbGreen" );
    SW_ASSERT_NOT_NULL( pMix );
    SW_EXPECT_EQUAL( 1, pMix->_outputs.getItemCount( "herbMix" ) );
    SW_EXPECT_TRUE( catalog.findCombine( "herbRed", "herbRed" ) == nullptr );

    const HorrorDialLockDef* pSafe = catalog.findDialLock( "safe" );
    SW_ASSERT_NOT_NULL( pSafe );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( pSafe->_listDigit.size() ) );
    SW_EXPECT_EQUAL( 4, pSafe->_listDigit[2] );
    const HorrorDeductionDef* pCulprit = catalog.findDeduction( "culprit" );
    SW_ASSERT_NOT_NULL( pCulprit );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( pCulprit->_listRequiredLink.size() ) );
    SW_EXPECT_TRUE( pCulprit->_listRequiredLink[1].isSame( "pantry", "gloves" ) );

    const HorrorRules& rules = catalog.getRules();
    SW_EXPECT_TRUE( rules._saveMode == HorrorSaveMode::InkRibbon );
    SW_EXPECT_EQUAL( 4, rules._gridWidth );
    SW_EXPECT_NEAR_EQUAL( 10.0f, rules._darknessDrain, 1.0e-4f );
}

SW_TEST_CASE( SurvivalHorrorTest, GridPlacesRotatesFindsFreeSpotsAndStacks )
{
    HorrorCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kHorrorTestXml, "SurvivalHorrorTest" ) );
    GridInventory grid;
    grid.initialize( catalog.makeShapeLookup(), 3, 3 );

    const int32 rifle = grid.placeItem( "rifle", 1, 0, 0, false );
    SW_EXPECT_TRUE( rifle > 0 );
    SW_EXPECT_TRUE( grid.placeItem( "case", 1, 0, 1, false ) > 0 );
    SW_EXPECT_FALSE( grid.canPlace( "case", 2, 1, false ) );    // 오른쪽으로 삐져나간다
    SW_EXPECT_FALSE( grid.canPlace( "handgun", 1, 1, false ) ); // 겹친다
    SW_EXPECT_EQUAL( 2, grid.countFreeCells() );

    // 남은 칸은 세로 한 줄 — 가로 2 칸 권총은 돌려서야 들어간다.
    int32 spotX    = -1;
    int32 spotY    = -1;
    bool  bRotated = false;
    SW_ASSERT_TRUE( grid.findFreeSpot( "handgun", spotX, spotY, bRotated ) );
    SW_EXPECT_EQUAL( 2, spotX );
    SW_EXPECT_EQUAL( 1, spotY );
    SW_EXPECT_TRUE( bRotated );
    const int32 handgun = grid.addItem( "handgun", 1 ) == 1 ? grid.findInstanceAt( 2, 2 ) : -1;
    SW_EXPECT_TRUE( handgun > 0 );
    SW_EXPECT_TRUE( grid.findInstance( handgun )->_bRotated == SW_TRUE );

    // 가득 찬 가방: 옮기기 · 돌리기는 막히면 그대로, 더 넣으면 0 개.
    SW_EXPECT_FALSE( grid.rotateItem( rifle ) );
    SW_EXPECT_FALSE( grid.moveItem( rifle, 0, 1, false ) );
    SW_EXPECT_EQUAL( 0, grid.addItem( "ammo", 15 ) );
    SW_EXPECT_EQUAL( 0, grid.findInstance( rifle )->_y );

    // 권총을 빼면 두 칸 — 탄약은 한 자리에 10 발까지 겹친다.
    SW_EXPECT_EQUAL( 1, grid.takeFromInstance( handgun, 1 ) );
    SW_EXPECT_EQUAL( 15, grid.addItem( "ammo", 15 ) );
    SW_EXPECT_EQUAL( 10, grid.findInstance( grid.findInstanceAt( 2, 1 ) )->_count );
    SW_EXPECT_EQUAL( 5, grid.findInstance( grid.findInstanceAt( 2, 2 ) )->_count );
    SW_EXPECT_EQUAL( 1, grid.addItem( "ammo", 1 ) ); // 새 자리가 없어도 5 발 자리에 겹친다
    SW_EXPECT_EQUAL( 16, grid.getItemCount( "ammo" ) );
    SW_EXPECT_FALSE( grid.removeItem( "ammo", 20 ) ); // 모자라면 하나도 빼지 않는다
    SW_EXPECT_TRUE( grid.removeItem( "ammo", 13 ) );  // 나중에 놓은 자리(6 발)부터 비운다
    SW_EXPECT_EQUAL( 3, grid.getItemCount( "ammo" ) );
    SW_EXPECT_EQUAL( -1, grid.findInstanceAt( 2, 2 ) );

    // 가방을 키우면 놓인 자리는 그대로.
    SW_EXPECT_FALSE( grid.growGrid( 2, 3 ) );
    SW_EXPECT_TRUE( grid.growGrid( 4, 3 ) );
    SW_EXPECT_EQUAL( rifle, grid.findInstanceAt( 2, 0 ) );
    SW_EXPECT_TRUE( grid.rotateItem( grid.findInstanceAt( 2, 1 ) ) ); // 1x1 탄약은 돌려도 같은 칸
}

SW_TEST_CASE( SurvivalHorrorTest, ItemBoxAndCombineRollBackWhenTheResultHasNoRoom )
{
    HorrorCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kHorrorTestXml, "SurvivalHorrorTest" ) );
    HorrorSession session;
    session.initialize( &catalog, nullptr, hashed_string() );
    GridInventory& grid = session.getInventory();
    SW_EXPECT_EQUAL( 2, grid.addItem( "herbGreen", 2 ) ); // (0,0) (1,0)
    SW_EXPECT_EQUAL( 1, grid.addItem( "herbRed", 1 ) );   // (2,0)
    SW_EXPECT_EQUAL( 5, grid.addItem( "junk", 9 ) );      // 남은 다섯 칸만
    SW_EXPECT_EQUAL( 0, grid.countFreeCells() );

    // 초록 + 빨강 → 혼합 약초(재료가 비운 자리에 들어간다).
    SW_EXPECT_TRUE( session.combineItems( grid.findInstanceAt( 0, 0 ), grid.findInstanceAt( 2, 0 ) ) );
    SW_EXPECT_EQUAL( 1, grid.getItemCount( "herbMix" ) );
    SW_EXPECT_EQUAL( 1, grid.getItemCount( "herbGreen" ) );
    SW_EXPECT_EQUAL( 0, grid.getItemCount( "herbRed" ) );

    // 혼합 + 초록 → 2x2 — 비는 칸이 한 줄뿐이라 들어갈 데가 없다: 아무것도 바뀌지 않는다.
    const int32 mixInstance   = grid.findInstanceAt( 0, 0 );
    const int32 greenInstance = grid.findInstanceAt( 1, 0 );
    SW_EXPECT_FALSE( session.combineItems( mixInstance, greenInstance ) );
    SW_EXPECT_EQUAL( 1, grid.getItemCount( "herbMix" ) );
    SW_EXPECT_EQUAL( 1, grid.getItemCount( "herbGreen" ) );
    SW_EXPECT_EQUAL( 0, grid.getItemCount( "herbSuper" ) );
    SW_EXPECT_FALSE( session.combineItems( grid.findInstanceAt( 0, 1 ), grid.findInstanceAt( 1, 1 ) ) ); // 잡동사니끼리는 레시피가 없다

    // 상자는 공유 보관함 — 잡동사니 넷을 맡기면 2x2 자리가 생기고, 그제서야 섞인다.
    for ( int32 x = 0; x < 4; ++x )
    {
        const int32 junkInstance = grid.findInstanceAt( x, 1 );
        if ( junkInstance > 0 && grid.findInstance( junkInstance )->_itemId == hashed_string( "junk" ) )
            SW_EXPECT_TRUE( session.storeInBox( junkInstance, 1 ) );
    }
    SW_EXPECT_EQUAL( 4, session.getItemBox().getItemCount( "junk" ) );
    SW_EXPECT_FALSE( session.storeInBox( grid.findInstanceAt( 3, 0 ), 2 ) ); // 한 개뿐인 자리에서 둘은 못 맡긴다
    SW_EXPECT_TRUE( session.combineItems( grid.findInstanceAt( 0, 0 ), grid.findInstanceAt( 1, 0 ) ) );
    SW_EXPECT_EQUAL( 1, grid.getItemCount( "herbSuper" ) );
    SW_EXPECT_EQUAL( 3, session.takeFromBox( "junk", 4 ) ); // 2x2 가 왼쪽 네 칸을 차지해 세 칸만 남았다 — 하나는 상자에 남는다
    SW_EXPECT_EQUAL( 1, session.getItemBox().getItemCount( "junk" ) );
}

SW_TEST_CASE( SurvivalHorrorTest, SavesNeedInkRibbonsOrRespectTheLimitAndAmmoIsScarce )
{
    HorrorCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kHorrorTestXml, "SurvivalHorrorTest" ) );
    HorrorSession session;
    session.initialize( &catalog, nullptr, hashed_string() );
    SW_EXPECT_TRUE( session.trySave() == HorrorSaveResult::NoSaveItem );
    SW_EXPECT_EQUAL( 2, session.getInventory().addItem( "ribbon", 2 ) );
    SW_EXPECT_TRUE( session.trySave() == HorrorSaveResult::Ok );
    SW_EXPECT_TRUE( session.trySave() == HorrorSaveResult::Ok );
    SW_EXPECT_TRUE( session.trySave() == HorrorSaveResult::NoSaveItem );
    SW_EXPECT_EQUAL( 2, session.getSaveCount() );

    HorrorCatalog limitedCatalog = catalog;
    HorrorRules   rules          = catalog.getRules();
    rules._saveMode              = HorrorSaveMode::Limited;
    rules._maxSaves              = 1;
    limitedCatalog.setRules( rules );
    HorrorSession limited;
    limited.initialize( &limitedCatalog, nullptr, hashed_string() );
    SW_EXPECT_TRUE( limited.trySave() == HorrorSaveResult::Ok );
    SW_EXPECT_TRUE( limited.trySave() == HorrorSaveResult::NoSavesLeft );

    // 탄약은 가방에 있는 만큼만, 회복은 한 번 쓰면 없어진다.
    SW_EXPECT_EQUAL( 7, session.getInventory().addItem( "ammo", 7 ) );
    SW_EXPECT_TRUE( session.tryConsumeAmmo( "ammo", 5 ) );
    SW_EXPECT_FALSE( session.tryConsumeAmmo( "ammo", 3 ) );
    SW_EXPECT_EQUAL( 2, session.getInventory().getItemCount( "ammo" ) );
    session.applyDamage( 60.0f );
    SW_EXPECT_EQUAL( 1, session.getInventory().addItem( "herbGreen", 1 ) );
    const int32 herb = session.getInventory().getItems().back()._instanceId;
    SW_EXPECT_TRUE( session.tryUseItem( herb ) );
    SW_EXPECT_NEAR_EQUAL( 65.0f, session.getHealth(), 1.0e-4f );
    SW_EXPECT_FALSE( session.tryUseItem( herb ) );
    SW_EXPECT_FALSE( session.tryUseItem( session.getInventory().getItems().front()._instanceId ) ); // 탄약은 "쓰는" 아이템이 아니다
}

SW_TEST_CASE( SurvivalHorrorTest, SanityFallsInDarknessAndSightingsWhileTheFlashlightHoldsItBack )
{
    HorrorCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kHorrorTestXml, "SurvivalHorrorTest" ) );
    HorrorSession session;
    session.initialize( &catalog, nullptr, hashed_string() );
    vector<HorrorEvent> listEvent;

    // 괴물 목격: 처음은 전부, 다시 보면 배율(0.25)만큼.
    SW_EXPECT_NEAR_EQUAL( 30.0f, session.witnessMonster( "zombie" ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 7.5f, session.witnessMonster( "zombie" ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, session.witnessMonster( "ghost" ), 1.0e-4f );
    SW_EXPECT_FALSE( session.isHallucinating() );
    SW_EXPECT_NEAR_EQUAL( 40.0f, session.witnessMonster( "elder" ), 1.0e-4f ); // 22.5 / 100 < 0.3
    SW_EXPECT_TRUE( session.isHallucinating() );
    SW_EXPECT_NEAR_EQUAL( 1.0f + 0.775f * 2.0f, session.computeAimSwayScale(), 1.0e-4f );
    session.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasEvent( listEvent, HorrorEvent::Kind::HallucinationStarted ) );

    SW_EXPECT_EQUAL( 1, session.getInventory().addItem( "sedative", 1 ) );
    SW_EXPECT_TRUE( session.tryUseItem( session.getInventory().getItems().back()._instanceId ) );
    SW_EXPECT_NEAR_EQUAL( 42.5f, session.getSanity().getValue(), 1.0e-4f );
    SW_EXPECT_FALSE( session.isHallucinating() );

    // 어둠: 빛이 없으면 초당 10. 손전등을 켜면 깎이지 않는다(규칙을 끄면 — 손전등이 없으면 — 이 시험은 진다).
    session.update( 1.0f, true );
    SW_EXPECT_NEAR_EQUAL( 32.5f, session.getSanity().getValue(), 1.0e-4f );
    SW_EXPECT_TRUE( session.trySetFlashlight( true ) );
    session.update( 1.0f, true );
    SW_EXPECT_NEAR_EQUAL( 32.5f, session.getSanity().getValue(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 18.0f, session.getBattery().getValue(), 1.0e-4f );

    // 배터리가 다 되면 꺼지고, 다시 켤 수 없으며, 어둠이 다시 깎는다.
    listEvent.clear();
    for ( int32 second = 0; second < 9; ++second )
        session.update( 1.0f, true );
    session.drainEvents( listEvent );
    SW_EXPECT_TRUE( hasEvent( listEvent, HorrorEvent::Kind::FlashlightDied ) );
    SW_EXPECT_FALSE( session.isFlashlightOn() );
    SW_EXPECT_FALSE( session.trySetFlashlight( true ) );
    const float32 beforeDark = session.getSanity().getValue();
    session.update( 1.0f, true );
    SW_EXPECT_NEAR_EQUAL( beforeDark - 10.0f, session.getSanity().getValue(), 1.0e-4f );
    session.update( 1.0f, false ); // 밝은 곳 — 지연(2 초)이 지나기 전에는 차지 않는다
    SW_EXPECT_NEAR_EQUAL( beforeDark - 10.0f, session.getSanity().getValue(), 1.0e-4f );

    SW_EXPECT_EQUAL( 1, session.getInventory().addItem( "battery", 1 ) );
    SW_EXPECT_TRUE( session.tryUseItem( session.getInventory().getItems().back()._instanceId ) );
    SW_EXPECT_TRUE( session.trySetFlashlight( true ) );
}

SW_TEST_CASE( SurvivalHorrorTest, KeysDialsAndSequencesOpenTheMansionThroughAreaGraphFlags )
{
    HorrorCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kHorrorTestXml, "SurvivalHorrorTest" ) );
    AreaGraph areaGraph;
    SW_ASSERT_TRUE( areaGraph.loadFromXmlText( kHorrorAreaXml, "SurvivalHorrorTest" ) );
    HorrorSession session;
    session.initialize( &catalog, &areaGraph, "hall" );
    SW_EXPECT_TRUE( areaGraph.isVisited( "hall" ) );

    SW_EXPECT_TRUE( session.tryMoveTo( "dining" ) );
    SW_EXPECT_TRUE( session.tryMoveTo( "hall" ) );
    SW_EXPECT_FALSE( session.tryMoveTo( "study" ) ); // 붉은 문은 잠겨 있다
    SW_EXPECT_TRUE( session.useKey( "redDoor" ) == HorrorPuzzleResult::MissingItem );
    SW_EXPECT_EQUAL( 1, session.getInventory().addItem( "keyRed", 1 ) );
    SW_EXPECT_TRUE( session.useKey( "redDoor" ) == HorrorPuzzleResult::Solved );
    SW_EXPECT_EQUAL( 0, session.getInventory().getItemCount( "keyRed" ) ); // 다 쓴 열쇠는 없어진다
    SW_EXPECT_TRUE( session.useKey( "redDoor" ) == HorrorPuzzleResult::AlreadySolved );
    SW_EXPECT_TRUE( session.tryMoveTo( "study" ) );
    SW_EXPECT_TRUE( session.getCurrentArea() == hashed_string( "study" ) );

    // 다이얼: 틀린 번호는 횟수를 깎고, 다 쓰면 맞는 번호도 듣지 않는다.
    SW_EXPECT_TRUE( session.enterDialCode( "safe", vector<int32>{ 1, 2, 3 } ) == HorrorPuzzleResult::Wrong );
    SW_EXPECT_TRUE( session.enterDialCode( "safe", vector<int32>{ 3, 1 } ) == HorrorPuzzleResult::Wrong );
    SW_EXPECT_TRUE( session.enterDialCode( "safe", vector<int32>{ 3, 1, 4 } ) == HorrorPuzzleResult::Solved );
    SW_EXPECT_TRUE( session.enterDialCode( "locker", vector<int32>{ 1, 1 } ) == HorrorPuzzleResult::Wrong );
    SW_EXPECT_TRUE( session.enterDialCode( "locker", vector<int32>{ 1, 2 } ) == HorrorPuzzleResult::LockedOut );
    SW_EXPECT_TRUE( session.enterDialCode( "locker", vector<int32>{ 9, 9 } ) == HorrorPuzzleResult::LockedOut );
    SW_EXPECT_FALSE( session.getFlags().hasFlag( "lockerOpen" ) );
    SW_EXPECT_FALSE( session.tryMoveTo( "vault" ) ); // 종 퍼즐이 아직이다

    // 순서 퍼즐: 틀리면 처음부터, 벌칙으로 정신력 5.
    SW_EXPECT_TRUE( session.pressSequenceStep( "bells", "low" ) == HorrorPuzzleResult::Progress );
    SW_EXPECT_TRUE( session.pressSequenceStep( "bells", "mid" ) == HorrorPuzzleResult::Wrong );
    SW_EXPECT_NEAR_EQUAL( 95.0f, session.getSanity().getValue(), 1.0e-4f );
    SW_EXPECT_TRUE( session.pressSequenceStep( "bells", "high" ) == HorrorPuzzleResult::Wrong ); // 처음부터라 high 는 틀린다
    SW_EXPECT_TRUE( session.pressSequenceStep( "bells", "low" ) == HorrorPuzzleResult::Progress );
    SW_EXPECT_TRUE( session.pressSequenceStep( "bells", "high" ) == HorrorPuzzleResult::Progress );
    SW_EXPECT_TRUE( session.pressSequenceStep( "bells", "mid" ) == HorrorPuzzleResult::Solved );
    SW_EXPECT_TRUE( session.tryMoveTo( "vault" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, areaGraph.computeExplorationRatio(), 1.0e-4f );
}

SW_TEST_CASE( SurvivalHorrorTest, ClueBoardDeductionNeedsTheRightLinksAndPunishesWrongGuesses )
{
    HorrorCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kHorrorTestXml, "SurvivalHorrorTest" ) );
    HorrorSession session;
    session.initialize( &catalog, nullptr, hashed_string() );

    SW_EXPECT_TRUE( session.readDocument( "diary" ) );
    SW_EXPECT_FALSE( session.readDocument( "diary" ) ); // 두 번째는 새 단서가 없다
    SW_EXPECT_TRUE( session.hasClue( "knife" ) );
    // 필요한 단서(pantry)를 아직 못 모았으면 벌칙 없이 거절.
    SW_EXPECT_TRUE( session.submitDeduction( "culprit", "butler" ) == HorrorPuzzleResult::MissingItem );
    SW_EXPECT_NEAR_EQUAL( 100.0f, session.getSanity().getValue(), 1.0e-4f );

    SW_EXPECT_TRUE( session.readDocument( "note" ) );
    SW_EXPECT_FALSE( session.linkClues( "knife", "letter" ) ); // 없는 단서는 잇지 못한다
    SW_EXPECT_TRUE( session.linkClues( "knife", "gloves" ) );
    SW_EXPECT_FALSE( session.linkClues( "gloves", "knife" ) ); // 이미 이어져 있다(순서 무관)

    // 답은 맞지만 연결이 모자라면 틀린 추리 — 찍어서 맞히지 못한다(이 규칙을 끄면 이 줄이 진다).
    SW_EXPECT_TRUE( session.submitDeduction( "culprit", "butler" ) == HorrorPuzzleResult::Wrong );
    SW_EXPECT_NEAR_EQUAL( 85.0f, session.getSanity().getValue(), 1.0e-4f );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( session.getClueLinks().size() ) ); // 틀리면 보드가 비워진다

    SW_EXPECT_TRUE( session.linkClues( "knife", "gloves" ) );
    SW_EXPECT_TRUE( session.linkClues( "pantry", "gloves" ) );
    SW_EXPECT_TRUE( session.submitDeduction( "culprit", "maid" ) == HorrorPuzzleResult::Wrong );
    SW_EXPECT_EQUAL( 2, session.getWrongDeductionCount() );
    SW_EXPECT_TRUE( session.linkClues( "knife", "gloves" ) );
    SW_EXPECT_TRUE( session.linkClues( "gloves", "pantry" ) );
    SW_EXPECT_TRUE( session.unlinkClues( "pantry", "gloves" ) );
    SW_EXPECT_TRUE( session.linkClues( "gloves", "pantry" ) );
    SW_EXPECT_TRUE( session.submitDeduction( "culprit", "butler" ) == HorrorPuzzleResult::Solved );
    SW_EXPECT_TRUE( session.getFlags().hasFlag( "caseSolved" ) );
    SW_EXPECT_TRUE( session.submitDeduction( "culprit", "maid" ) == HorrorPuzzleResult::AlreadySolved );
    SW_EXPECT_NEAR_EQUAL( 70.0f, session.getSanity().getValue(), 1.0e-4f );
}

SW_TEST_CASE( SurvivalHorrorTest, TurnBasedEncounterIsDeterministicAndEndsInVictoryOrDefeat )
{
    HorrorCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kHorrorTestXml, "SurvivalHorrorTest" ) );
    const HorrorMonsterDef* pZombie = catalog.findMonster( "zombie" );
    SW_ASSERT_NOT_NULL( pZombie );

    vector<HorrorInvestigator> listInvestigator( 2 );
    listInvestigator[0]._actorId = 1;
    listInvestigator[0]._health  = 4;
    listInvestigator[1]._actorId = 2;
    listInvestigator[1]._health  = 3;

    // 같은 씨앗이면 차례마다 같은 결과.
    HorrorEncounter first;
    HorrorEncounter second;
    first.initialize( *pZombie, listInvestigator, 77u );
    second.initialize( *pZombie, listInvestigator, 77u );
    int32 turn = 0;
    for ( ; turn < 200 && first.getState() == HorrorEncounterState::Ongoing; ++turn )
    {
        const HorrorTurnResult a = first.playNextTurn();
        const HorrorTurnResult b = second.playNextTurn();
        SW_EXPECT_EQUAL( a._actorId, b._actorId );
        SW_EXPECT_EQUAL( a._combatSuccesses, b._combatSuccesses );
        SW_EXPECT_EQUAL( a._horrorSuccesses, b._horrorSuccesses );
        SW_EXPECT_EQUAL( a._targetId, b._targetId );
        if ( a._actorId == HorrorEncounter::kMonsterActorId && a._targetId >= 0 && turn == 0 )
            SW_EXPECT_EQUAL( 2, a._targetId ); // 첫 공격은 체력이 낮은 조사자에게
    }
    SW_EXPECT_TRUE( turn < 200 );
    SW_EXPECT_TRUE( first.getState() != HorrorEncounterState::Ongoing );
    SW_EXPECT_TRUE( first.getState() == second.getState() );

    // 늘 성공하는 주사위(면 1 이상)와 힘 4 — 첫 조사자 차례에 강인함 4 를 다 깎는다.
    listInvestigator[0]._strength = 4;
    listInvestigator[1]._strength = 4;
    HorrorEncounter sure;
    sure.initialize( *pZombie, listInvestigator, 5u, 1 );
    for ( int32 guard = 0; guard < 10 && sure.getState() == HorrorEncounterState::Ongoing; ++guard )
        (void)sure.playNextTurn();
    SW_EXPECT_TRUE( sure.getState() == HorrorEncounterState::Victory );
    SW_EXPECT_EQUAL( 0, sure.getMonsterToughness() );

    // 힘 0 이면 괴물은 쓰러지지 않고 조사자가 모두 빠진다.
    listInvestigator[0]._strength = 0;
    listInvestigator[1]._strength = 0;
    HorrorEncounter hopeless;
    hopeless.initialize( *pZombie, listInvestigator, 5u );
    for ( int32 guard = 0; guard < 200 && hopeless.getState() == HorrorEncounterState::Ongoing; ++guard )
        (void)hopeless.playNextTurn();
    SW_EXPECT_TRUE( hopeless.getState() == HorrorEncounterState::Defeat );
    SW_EXPECT_EQUAL( 4, hopeless.getMonsterToughness() );
}
