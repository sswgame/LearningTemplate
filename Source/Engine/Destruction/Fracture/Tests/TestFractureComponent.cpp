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

/**
 * @brief [FractureComponentTest] 맞은 그룹이 없거나 앵커 그룹이면 사건 자세는 그룹 번호 0 이 아니라 비어 있고, 떨어진 그룹이면 그 번호와 자세가 담긴다
 */
SW_TEST_CASE( FractureComponentTest, EventGroupPoseIsEmptyWithoutFallenGroup )
{
    SW_EXPECT_FALSE( sw::FractureComponentBase::makeEventGroupPose( nullptr ).has_value() );
    sw::FractureGroupRuntime runtime;
    runtime._groupID   = 4;
    runtime._position  = sw::float3{ 1.0f, 2.0f, 3.0f };
    runtime._bAnchored = SW_TRUE;
    SW_EXPECT_FALSE( sw::FractureComponentBase::makeEventGroupPose( &runtime ).has_value() );
    runtime._bAnchored                             = SW_FALSE;
    const sw::optional<sw::FractureGroupPose> pose = sw::FractureComponentBase::makeEventGroupPose( &runtime );
    SW_REQUIRE_TRUE( pose.has_value() );
    SW_EXPECT_EQUAL( pose->_groupID, 4u );
    SW_EXPECT_EQUAL( pose->_position._y, 2.0f );
}
