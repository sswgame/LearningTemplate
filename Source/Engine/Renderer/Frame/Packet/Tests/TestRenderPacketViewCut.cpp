/**
 * @file TestRenderPacketViewCut.cpp
 * @brief 주 시점이 지난 프레임과 이어지는가(`RenderPacketViewSetup::isMainViewCut`)의 규칙입니다 — 이어지지 않으면 시간 누적 기록을 버린다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketViewSetup.h"

using namespace sw;

/** @brief [RenderPacketViewCutTest] 같은 카메라 · 같은 원점만 이어진다 — 다른 카메라(시퀀서 컷 · 씬 전환)와 원점 이동은 컷이다. 변이: 둘 중 하나의 비교를 빼면 그 단언이 진다. */
SW_TEST_CASE( RenderPacketViewCutTest, CameraChangeAndOriginShiftAreCuts )
{
    const double3 origin{ 100.0, 0.0, -200.0 };
    SW_EXPECT_FALSE( RenderPacketViewSetup::isMainViewCut( 7u, 7u, origin, origin ) );
    SW_EXPECT_TRUE( RenderPacketViewSetup::isMainViewCut( 7u, 8u, origin, origin ) );
    SW_EXPECT_TRUE( RenderPacketViewSetup::isMainViewCut( 7u, 7u, origin, double3{ 1100.0, 0.0, -200.0 } ) );
    SW_EXPECT_TRUE( RenderPacketViewSetup::isMainViewCut( 0u, 7u, origin, origin ) ); // 카메라가 없던 프레임 뒤
}
