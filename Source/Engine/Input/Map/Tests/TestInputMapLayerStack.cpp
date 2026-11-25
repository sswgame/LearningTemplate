// InputMapLayerStackTest — pushLayer 의 칸은 넣은 쪽이 소유한다: 풀리면 그 칸만 빠지고, 막는 칸은 나중에 넣은 레이어에도 막는다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"

using namespace sw;

/**
 * @brief [InputMapLayerStackTest] 꺼진 레이어를 push 했다가 풀면 다시 꺼진다 — 빈 스택은 켜진 레이어를 모두 살리므로 켬이 남으면 그 레이어가 계속 발화한다
 */
SW_TEST_CASE( InputMapLayerStackTest, ReleaseRestoresTheEnabledState )
{
    InputMap            map;
    const hashed_string overlay( "Overlay" );
    const hashed_string gameplay( "Gameplay" );
    map.registerLayer( gameplay );
    map.registerLayer( overlay, 0, false );
    SW_REQUIRE_FALSE( map.isLayerEnabled( overlay ) );

    Subscription pushed = map.pushLayer( overlay );
    SW_EXPECT_TRUE( map.isLayerEnabled( overlay ) );
    SW_EXPECT_FALSE( map.isLayerEnabled( gameplay ) ); // 스택이 차면 스택의 레이어만 산다
    pushed.reset();
    SW_EXPECT_FALSE( map.isLayerEnabled( overlay ) );
    SW_EXPECT_TRUE( map.isLayerEnabled( gameplay ) );

    const hashed_string created( "CreatedByPush" );
    pushed = map.pushLayer( created );
    SW_EXPECT_TRUE( map.isLayerEnabled( created ) );
    pushed.reset();
    SW_EXPECT_TRUE( map.hasLayer( created ) );
    SW_EXPECT_FALSE( map.isLayerEnabled( created ) );
}

/**
 * @brief [InputMapLayerStackTest] 같은 레이어를 둘이 넣으면 하나가 풀려도 남은 쪽이 켜 둔다(옛 코드는 먼저 끝난 쪽의 pop 이 남의 것까지 껐다)
 */
SW_TEST_CASE( InputMapLayerStackTest, TwoOwnersOfOneLayer )
{
    InputMap            map;
    const hashed_string cutscene( "Cutscene" );
    const hashed_string gameplay( "Gameplay" );
    map.registerLayer( gameplay );
    map.registerLayer( cutscene, 0, false );
    Subscription       firstBlock   = map.pushLayer( cutscene, true );
    Subscription       firstEnable  = map.enableLayer( cutscene );
    const Subscription secondBlock  = map.pushLayer( cutscene, true );
    const Subscription secondEnable = map.enableLayer( cutscene );
    firstBlock.reset();
    firstEnable.reset();
    SW_EXPECT_TRUE( map.isLayerEnabled( cutscene ) );
    SW_EXPECT_FALSE( map.isLayerEnabled( gameplay ) );
}

/**
 * @brief [InputMapLayerStackTest] 막는 칸은 나중에 넣은 막지 않는 레이어도 막고(컷씬 중 다시 빙의한 폰), 풀리면 그 레이어가 산다. 늘 켜지는 레이어는 막지 않는다
 */
SW_TEST_CASE( InputMapLayerStackTest, BlockingEntryStaysOnTop )
{
    InputMap            map;
    const hashed_string cutscene( "Cutscene" );
    const hashed_string onFoot( "OnFoot" );
    const hashed_string always( "Always" );
    map.registerLayer( onFoot, 0, false );
    map.registerLayer( always, 0, true, false, true );
    Subscription       block = map.pushLayer( cutscene, true );
    const Subscription pawn  = map.pushLayer( onFoot );
    SW_EXPECT_FALSE( map.isLayerEnabled( onFoot ) );
    SW_EXPECT_TRUE( map.isLayerEnabled( always ) );
    block.reset();
    SW_EXPECT_TRUE( map.isLayerEnabled( onFoot ) );
}

/**
 * @brief [InputMapLayerStackTest] 맵을 다시 읽어도 넣은 칸은 남는다(폰은 다시 빙의하지 않는다)
 */
SW_TEST_CASE( InputMapLayerStackTest, EntriesSurviveReload )
{
    const utf8* const   pText = R"(<InputMap><layers><layer name="Gameplay"/><layer name="OnFoot" enabled="0"/></layers></InputMap>)";
    InputMap            map;
    const hashed_string onFoot( "OnFoot" );
    SW_REQUIRE_TRUE( map.loadFromXMLText( pText, "InputMapLayerStackTest" ) );
    Subscription pawn = map.pushLayer( onFoot );
    SW_REQUIRE_TRUE( map.loadFromXMLText( pText, "InputMapLayerStackTest" ) );
    SW_EXPECT_TRUE( map.isLayerEnabled( onFoot ) );
    pawn.reset();
    SW_EXPECT_FALSE( map.isLayerEnabled( onFoot ) );
}

/**
 * @brief [InputMapLayerStackTest] 공유 레이어는 나중에 연 사용자의 맵에도 걸리고, 주 사용자만의 칸(폰 레이어)은 다른 사용자에게 베끼지 않으며, 풀리면 모두에게서 사라진다
 */
SW_TEST_CASE( InputMapLayerStackTest, SharedLayerCoversEveryUser )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "shared.input.xml" ), R"(<InputMap><layers><layer name="Gameplay"/><layer name="OnFoot" enabled="0"/></layers></InputMap>)" ) );
    InputManager        input;
    const hashed_string cutscene( "Cutscene" );
    const hashed_string onFoot( "OnFoot" );
    const hashed_string gameplay( "Gameplay" );
    SW_REQUIRE_TRUE( input.initialize() );
    SW_REQUIRE_TRUE( input.getInputMap().loadBundle( vector<AssetID>{ test::ScopedScratchDomain::makeID( "shared.input.xml" ) } ) );
    const Subscription pawn  = input.getInputMap().pushLayer( onFoot );
    Subscription       block = input.pushSharedLayer( cutscene, true );
    SW_REQUIRE_TRUE( input.openLocalUsers( 2 ) );
    InputMap* const pSecond = input.findUserMap( 1 );
    SW_REQUIRE_NOT_NULL( pSecond );
    SW_EXPECT_FALSE( input.getInputMap().isLayerEnabled( onFoot ) );
    SW_EXPECT_FALSE( pSecond->isLayerEnabled( gameplay ) ); // 컷씬이 모두를 막는다
    block.reset();
    SW_EXPECT_TRUE( input.getInputMap().isLayerEnabled( onFoot ) );
    SW_EXPECT_TRUE( pSecond->isLayerEnabled( gameplay ) ); // 주 사용자 폰의 칸 · 컷씬의 사본이 남아 있으면 꺼져 있다
    input.closeLocalUsers();
    input.shutdown();
}

/**
 * @brief [InputMapBindingRevisionTest] 재배치 · 되돌리기가 번호를 올린다 — 화면의 액션 글리프가 같은 장치에서도 새 키로 바뀐다
 */
SW_TEST_CASE( InputMapBindingRevisionTest, RebindAdvancesTheRevision )
{
    InputMap            map;
    const hashed_string jump( "Jump" );
    map.bind( jump, InputSlot::fromKey( Key::Space ), ActionTrigger::Pressed, hashed_string( "Gameplay" ) );
    uint32 revision = map.getBindingRevision();
    SW_REQUIRE_TRUE( map.rebindKey( jump, Key::J, 0 ) );
    SW_EXPECT_TRUE( map.getBindingRevision() != revision );
    revision = map.getBindingRevision();
    SW_REQUIRE_TRUE( map.resetActionToDefault( jump ) );
    SW_EXPECT_TRUE( map.getBindingRevision() != revision );
}
