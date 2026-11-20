// InputMapLayerStackTest — pushLayer · popLayer 는 한 쌍이다: pop 이 push 가 바꾼 켬을 되돌려, 빈 스택이 push 로 켠 레이어를 살려 두지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Map/InputMap.h"

using namespace sw;

/**
 * @brief [InputMapLayerStackTest] 꺼진 레이어를 push 했다가 pop 하면 다시 꺼진다 — 빈 스택은 켜진 레이어를 모두 살리므로 켬이 남으면 그 레이어가 계속 발화한다
 */
SW_TEST_CASE( InputMapLayerStackTest, PopRestoresTheEnabledStateBeforePush )
{
    InputMap            map;
    const hashed_string overlay( "Overlay" );
    const hashed_string gameplay( "Gameplay" );
    map.registerLayer( gameplay );
    map.registerLayer( overlay, 0, false );
    SW_REQUIRE_FALSE( map.isLayerEnabled( overlay ) );

    map.pushLayer( overlay );
    SW_EXPECT_TRUE( map.isLayerEnabled( overlay ) );
    SW_EXPECT_FALSE( map.isLayerEnabled( gameplay ) ); // 스택이 차면 스택의 레이어만 산다
    map.popLayer( overlay );
    SW_EXPECT_FALSE( map.isLayerEnabled( overlay ) ); // 음성 대조: 고치기 전에는 켬이 남아 빈 스택에서 살아났다
    SW_EXPECT_TRUE( map.isLayerEnabled( gameplay ) );

    map.pushLayer( overlay );
    map.popLayer(); // 이름 없는 pop 도 같은 뜻이다
    SW_EXPECT_FALSE( map.isLayerEnabled( overlay ) );
}

/**
 * @brief [InputMapLayerStackTest] 켜져 있던 레이어는 pop 뒤에도 켜져 있고, push 가 만든 없던 레이어는 pop 뒤에 꺼진다
 */
SW_TEST_CASE( InputMapLayerStackTest, PopKeepsPriorEnabledAndDisablesCreatedLayers )
{
    InputMap            map;
    const hashed_string hub( "Hub" );
    const hashed_string created( "CreatedByPush" );
    map.registerLayer( hub );
    map.pushLayer( hub );
    map.popLayer( hub );
    SW_EXPECT_TRUE( map.isLayerEnabled( hub ) );

    SW_REQUIRE_FALSE( map.hasLayer( created ) );
    map.pushLayer( created );
    SW_EXPECT_TRUE( map.hasLayer( created ) );
    map.popLayer( created );
    SW_EXPECT_FALSE( map.isLayerEnabled( created ) );
}

/**
 * @brief [InputMapLayerStackTest] 이미 스택에 있는 레이어를 다시 넣으면 맨 위로 옮기고, 처음 push 전의 켬을 기억해 pop 이 그리로 되돌린다
 */
SW_TEST_CASE( InputMapLayerStackTest, RepushKeepsTheFirstSavedState )
{
    InputMap            map;
    const hashed_string cutscene( "Cutscene" );
    const hashed_string menu( "Menu" );
    map.registerLayer( cutscene, 0, false );
    map.registerLayer( menu );
    map.pushLayer( cutscene, true );
    map.pushLayer( menu );
    map.pushLayer( cutscene, true );
    vector<hashed_string> listStack;
    map.collectLayerStack( listStack );
    SW_REQUIRE_TRUE( listStack.size() == 2 );
    SW_EXPECT_TRUE( listStack[1] == cutscene );
    map.popLayer( cutscene );
    map.popLayer( menu );
    SW_EXPECT_FALSE( map.isLayerEnabled( cutscene ) );
    SW_EXPECT_TRUE( map.isLayerEnabled( menu ) );
    map.collectLayerStack( listStack );
    SW_EXPECT_TRUE( listStack.empty() );
}

/**
 * @brief [InputMapLayerStackTest] 스택에 없는 레이어의 pop 은 아무것도 바꾸지 않는다(다른 레이어의 켬을 건드리지 않는다)
 */
SW_TEST_CASE( InputMapLayerStackTest, PopOfAnAbsentLayerChangesNothing )
{
    InputMap            map;
    const hashed_string hub( "Hub" );
    const hashed_string other( "Other" );
    map.registerLayer( hub );
    map.registerLayer( other, 0, false );
    map.popLayer( other );
    map.popLayer();
    SW_EXPECT_TRUE( map.isLayerEnabled( hub ) );
    SW_EXPECT_FALSE( map.isLayerEnabled( other ) );
}
