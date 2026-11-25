// RigidBodyStateTest — 강체의 속도가 상태 코덱으로 실리는지 본다(멈춘 바디는 쓰는 것이 없다, nogpu — 바디 없이 값 칸으로).
#include "pch.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Serialization/Format/Archive.h"

using namespace sw;

/** @brief [RigidBodyStateTest] 멈춘 바디는 빈 바이트이고, 되돌린 속도는 바디가 서기 전에 다시 떠도 같은 바이트다 */
SW_TEST_CASE( RigidBodyStateTest, RestoredVelocityIsWrittenAgainBeforeTheBodyExists )
{
    GameObjectManager   manager;
    RigidBodyComponent* pBody = manager.createGameObject( hashed_string( "Crate" ) )->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pBody );
    Archive resting;
    pBody->writeState( resting );
    SW_EXPECT_EQUAL( uint64{ 0 }, static_cast<uint64>( resting.getSize() ) );

    Archive moving;
    StateIO::writeHeader( moving, FourCCUtil::make( "RBST" ), 0 );
    moving << float3{ 1.0f, 2.0f, 3.0f } << float3{ 0.0f, 4.0f, 0.0f };
    vector<uint8> bytes;
    moving.writeData( bytes );
    const vector<uint8> expected = bytes;
    pBody->restoreState( std::move( bytes ) );
    SW_EXPECT_NEAR( 2.0f, pBody->getLinearVelocity()._y, 1.0e-6f );
    SW_EXPECT_NEAR( 4.0f, pBody->getAngularVelocity()._y, 1.0e-6f );
    Archive again;
    pBody->writeState( again );
    vector<uint8> writtenAgain;
    again.writeData( writtenAgain );
    SW_EXPECT_TRUE( writtenAgain == expected );
}
