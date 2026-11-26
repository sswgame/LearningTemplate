#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"

// 추가 뷰 자리 수(RN-16) — 설정 파일(EngineConfig `_render._maxExtraViewCount`)이 정본이고, 구조 상한(`kExtraRenderViewCapacity`)을 넘거나 0 이면 쓰지 않는다.

using namespace sw;

/** @brief [RenderViewCapacityTest] 자리 수는 1 ~ 구조 상한만 받는다(0 은 빠진 값이다 — 기본값으로 대신하지 않는다) */
SW_TEST_CASE( RenderViewCapacityTest, MaxExtraViewCountRange )
{
    SW_EXPECT_FALSE( RenderViewCollector::isValidMaxExtraViewCount( 0 ) );
    SW_EXPECT_TRUE( RenderViewCollector::isValidMaxExtraViewCount( 1 ) );
    SW_EXPECT_TRUE( RenderViewCollector::isValidMaxExtraViewCount( kExtraRenderViewCapacity ) );
    SW_EXPECT_FALSE( RenderViewCollector::isValidMaxExtraViewCount( kExtraRenderViewCapacity + 1 ) );
}

/** @brief [RenderViewCapacityTest] 엔진 설정의 자리 수가 범위 안이고 수집 · 렌더러가 같은 값을 읽는다 */
SW_TEST_CASE( RenderViewCapacityTest, MaxExtraViewCountComesFromEngineConfig )
{
    const uint32 configured = engine::getSubsystem<EngineConfig>()._render._maxExtraViewCount;
    SW_EXPECT_TRUE( RenderViewCollector::isValidMaxExtraViewCount( configured ) );
    SW_EXPECT_EQUAL( configured, RenderViewCollector::getMaxExtraViewCount() );
}
