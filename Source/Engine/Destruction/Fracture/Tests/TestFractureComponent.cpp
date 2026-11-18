#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Destruction/Fracture/FractureComponent.h"

// FractureComponentTest — 파괴 컴포넌트의 질의는 엔진(씬 · 물리) 없이 정해진다.

/**
 * @brief [FractureComponentTest] 무효 바디 · 모르는 바디의 그룹은 그룹 번호 0 이 아니라 비어 있다
 */
SW_TEST_CASE( FractureComponentTest, UnknownBodyHasNoGroup )
{
    const sw::FractureComponent component;
    SW_EXPECT_FALSE( component.findGroupOfBody( sw::PhysicsBodyHandle{} ).has_value() );
    SW_EXPECT_FALSE( component.findGroupOfBody( sw::PhysicsBodyHandle::fromPacked( ( uint64( 1 ) << 32 ) | 7u ) ).has_value() );
}
