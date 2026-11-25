#include "pch.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Destruction/Fracture/FractureComponent.h"
#include "Engine/Serialization/Format/Archive.h"

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
    SW_EXPECT_FALSE( sw::FractureBaseComponent::makeEventGroupPose( nullptr, sw::WorldOrigin{} ).has_value() );
    sw::FractureGroupRuntime runtime;
    runtime._groupID   = 4;
    runtime._position  = sw::float3{ 1.0f, 2.0f, 3.0f };
    runtime._bAnchored = SW_TRUE;
    SW_EXPECT_FALSE( sw::FractureBaseComponent::makeEventGroupPose( &runtime, sw::WorldOrigin{} ).has_value() );
    runtime._bAnchored                             = SW_FALSE;
    const sw::optional<sw::FractureGroupPose> pose = sw::FractureBaseComponent::makeEventGroupPose( &runtime, sw::WorldOrigin{
                                                                                                                  sw::double3{ 100.0, 0.0, 0.0 }
    } );
    SW_REQUIRE_TRUE( pose.has_value() );
    SW_EXPECT_EQUAL( pose->_groupID, 4u );
    SW_EXPECT_EQUAL( pose->_position._x, 101.0f ); // 보내는 자리는 절대다
    SW_EXPECT_EQUAL( pose->_position._y, 2.0f );
}

/**
 * @brief [FractureComponentTest] 되돌린 부서진 상태는 시작하는 물리 프레임까지 보관되고 그 동안은 그 바이트가 곧 상태다. 맞은 적 없는 것은 보관할 것이 없다
 */
SW_TEST_CASE( FractureComponentTest, RestoredStateWaitsForTheFirstPhysicsFrame )
{
    sw::FractureComponent intact;
    sw::Archive           written;
    intact.writeState( written );
    sw::vector<uint8> bytes;
    written.writeData( bytes );
    sw::FractureComponent restored;
    restored.restoreState( std::move( bytes ) );
    SW_EXPECT_TRUE( restored.getPendingStateBytes().empty() );
    SW_EXPECT_FALSE( restored.hasPendingDamage() );

    sw::Archive broken;
    sw::StateIO::writeHeader( broken, sw::FourCCUtil::make( "FRST" ), 0 );
    broken << sw::vector<uint8>{ 1, 2, 3 };
    sw::vector<uint8> snapshot;
    broken.writeData( snapshot );
    const sw::vector<uint8> expected = snapshot;
    restored.restoreState( std::move( snapshot ) );
    SW_EXPECT_TRUE( restored.hasPendingDamage() );
    SW_EXPECT_TRUE( restored.getPendingStateBytes() == expected );
}
