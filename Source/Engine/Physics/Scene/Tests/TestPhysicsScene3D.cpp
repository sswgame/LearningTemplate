// PhysicsScene3DTest — 3D 물리 씬의 용량 설정 · 임시 블록 폴백(감사 S-2), 캐릭터 속 바디 · 캐릭터끼리 막기(S-7), 캐릭터 일괄 이동(S-8). 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

using namespace sw;

namespace
{
    struct TestPhysicsScene3DInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 윗면이 y = 0 인 넓은 정적 바닥을 만듭니다. */
        static RigidBodyComponent* addFloor( GameObjectManager& manager )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Floor" ) );
            if ( pObject == nullptr )
                return nullptr;
            RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = float3{ 20.0f, 0.5f, 20.0f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLocalPosition( float3{ 0.0f, -0.5f, 0.0f } );
            return pBody;
        }

        /** @brief 발이 @p position 인 캐릭터를 만듭니다(속 바디는 기본값 그대로). */
        static CharacterControllerComponent* addWalker( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            CharacterControllerComponent* pCharacter = pObject->addComponent<CharacterControllerComponent>();
            if ( pCharacter == nullptr )
                return nullptr;
            pCharacter->setLocalPosition( position );
            return pCharacter;
        }

        static uint32 countPhase( const vector<PhysicsContactEvent3D>& listEvent, PhysicsBodyHandle body, PhysicsContactPhase phase )
        {
            uint32 count = 0;
            for ( const PhysicsContactEvent3D& event : listEvent )
            {
                const bool bInvolved = event._bodyA == body || event._bodyB == body;
                count += ( bInvolved && event._phase == phase ) ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace

/**
 * @brief [PhysicsScene3DTest] 3D 씬 용량(바디 · 쌍 · 접촉 수, 임시 블록 바이트)을 적지 않은 설정은 읽을 때 오류다 — 코드 기본값으로 채우지 않는다(감사 S-2)
 * @details 예전에는 네 값이 코드 상수(65536 · 65536 · 32768 · 16 MB)라 데이터로 바꿀 수 없었다.
 */
SW_TEST_CASE( PhysicsScene3DTest, MissingCapacityIsLoadError )
{
    // 빠진 칸은 반사의 `Min = 1` 이 먼저 거절한다(0 은 범위 밖) — validate 의 용량 검사까지 가지 않는다.
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_LOG( "PhysicsSettings.PhysicsSettingsTextCould" );
    PhysicsSettings settings;
    SW_EXPECT_FALSE( settings.loadFromXMLText( R"(<PhysicsSettings>
        <_listLayer><PhysicsLayerDef _name="Default"><_listCollidesWith><item>Default</item></_listCollidesWith></PhysicsLayerDef></_listLayer>
        <_listMaterial><PhysicsMaterialDef _name="Default" /></_listMaterial>
    </PhysicsSettings>)" ) );
    SW_EXPECT_TRUE( settings.loadFromXMLText( R"(<PhysicsSettings _maxBodyCount="1024" _maxBodyPairCount="1024" _maxContactCount="1024" _tempAllocatorBytes="65536" _maxDebrisBodyCount="64">
        <_listLayer><PhysicsLayerDef _name="Default"><_listCollidesWith><item>Default</item></_listCollidesWith></PhysicsLayerDef></_listLayer>
        <_listMaterial><PhysicsMaterialDef _name="Default" /></_listMaterial>
    </PhysicsSettings>)" ) );
}

/**
 * @brief [PhysicsScene3DTest] 임시 블록보다 큰 스텝(깨어 있는 상자 더미)도 멈추지 않고 힙으로 이어 가며, 처음 넘친 스텝에 한 번 경고한다(감사 S-2)
 * @details 블록을 16 KB 로 줄이고 서로 닿은 상자 8×8×8 을 떨어뜨린다 — 접촉 제약 · 섬 · 갱신 문맥만으로 블록을 넘는다.
 *          예전 `TempAllocatorImpl` 은 넘치는 할당에서 `std::abort()` 였다(크기 16 MB 고정 — 이 테스트의 설정 필드도 없었다).
 */
SW_TEST_CASE( PhysicsScene3DTest, LargeActivePileDoesNotAbort )
{
    SW_EXPECT_LOG( "JoltPhysicsScene.TempAllocatorFallback" );
    const PhysicsSystem& system              = engine::getSubsystem<PhysicsSystem>();
    PhysicsSettings      settings            = system.getSettings();
    settings._tempAllocatorBytes             = 16 * 1024;
    const unique_ptr<IPhysicsScene3D> pScene = system.createScene3D( settings );
    SW_REQUIRE_NOT_NULL( pScene.get() );

    PhysicsShapeDesc3D box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 0.5f, 0.5f, 0.5f };
    vector<PhysicsBodyDesc3D> listDesc;
    PhysicsBodyDesc3D         floor;
    floor._listShape.push_back( box );
    floor._listShape[0]._halfExtents = float3{ 20.0f, 0.5f, 20.0f };
    floor._position                  = float3{ 0.0f, -0.5f, 0.0f };
    floor._type                      = PhysicsBodyType::Static;
    listDesc.push_back( floor );
    constexpr uint32 kSide = 8;
    for ( uint32 x = 0; x < kSide; ++x )
    {
        for ( uint32 y = 0; y < kSide; ++y )
        {
            for ( uint32 z = 0; z < kSide; ++z )
            {
                PhysicsBodyDesc3D crate;
                crate._listShape.push_back( box );
                crate._position = float3{ static_cast<float32>( x ), 0.5f + static_cast<float32>( y ), static_cast<float32>( z ) };
                listDesc.push_back( crate );
            }
        }
    }
    vector<PhysicsBodyHandle> listBody;
    pScene->createBodies( listDesc, listBody );
    SW_REQUIRE_EQUAL( listDesc.size(), listBody.size() );
    for ( uint32 step = 0; step < 10; ++step )
    {
        pScene->step( TestPhysicsScene3DInternal::kFrame );
    }
    SW_EXPECT_EQUAL( static_cast<uint32>( listDesc.size() ), pScene->getBodyCount() );
    float3     position{};
    quaternion rotation{};
    SW_REQUIRE_TRUE( pScene->getBodyTransform( listBody.back(), position, rotation ) );
    SW_EXPECT_TRUE( position._y > 0.0f && position._y < static_cast<float32>( kSide ) + 1.0f ); // 더미가 무너지지도 날아가지도 않았다
}

/**
 * @brief [PhysicsScene3DTest] 일괄 이동(`moveCharacters`)은 칸마다 `moveCharacter` 와 같은 결과다(감사 S-8)
 */
SW_TEST_CASE( PhysicsScene3DTest, MoveCharactersMatchesMoveCharacter )
{
    const PhysicsSystem&              system  = engine::getSubsystem<PhysicsSystem>();
    const unique_ptr<IPhysicsScene3D> pSingle = system.createScene3D();
    const unique_ptr<IPhysicsScene3D> pBatch  = system.createScene3D();
    SW_REQUIRE_NOT_NULL( pSingle.get() );
    SW_REQUIRE_NOT_NULL( pBatch.get() );
    PhysicsCharacterDesc3D desc;
    desc._position                       = float3{ -2.0f, 0.0f, 0.0f };
    const PhysicsCharacterHandle singleA = pSingle->createCharacter( desc );
    const PhysicsCharacterHandle batchA  = pBatch->createCharacter( desc );
    desc._position                       = float3{ 2.0f, 0.0f, 0.0f };
    const PhysicsCharacterHandle singleB = pSingle->createCharacter( desc );
    const PhysicsCharacterHandle batchB  = pBatch->createCharacter( desc );
    const float3                 velocityA{ 1.0f, 0.0f, 0.5f };
    const float3                 velocityB{ -1.0f, 0.0f, 0.0f };
    const PhysicsCharacterMove3D arrMove[] = {
        {batchA, velocityA},
        {batchB, velocityB}
    };
    for ( uint32 step = 0; step < 30; ++step )
    {
        (void)pSingle->moveCharacter( singleA, velocityA, TestPhysicsScene3DInternal::kFrame );
        (void)pSingle->moveCharacter( singleB, velocityB, TestPhysicsScene3DInternal::kFrame );
        pBatch->moveCharacters( arrMove, TestPhysicsScene3DInternal::kFrame );
    }
    PhysicsCharacterState3D single{};
    PhysicsCharacterState3D batch{};
    SW_REQUIRE_TRUE( pSingle->getCharacterState( singleA, single ) );
    SW_REQUIRE_TRUE( pBatch->getCharacterState( batchA, batch ) );
    SW_EXPECT_TRUE( single._position == batch._position );
    SW_REQUIRE_TRUE( pSingle->getCharacterState( singleB, single ) );
    SW_REQUIRE_TRUE( pBatch->getCharacterState( batchB, batch ) );
    SW_EXPECT_TRUE( single._position == batch._position );
}

/**
 * @brief [PhysicsScene3DTest] 마주 걷는 캐릭터 둘은 서로 막혀 지나치지 않는다(감사 S-7 — 씬의 캐릭터 대 캐릭터 충돌)
 * @details 3 m 떨어진 둘이 2 m/s 로 1.5 초 걸으면 막는 것이 없을 때 자리를 바꾼다(예전: 캐릭터는 이동 질의로만 세계를 보고 다른 캐릭터는 바디가 아니었다).
 */
SW_TEST_CASE( PhysicsScene3DTest, CharactersWalkingTowardKeepApart )
{
    using Internal = TestPhysicsScene3DInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::addFloor( manager ) );
    CharacterControllerComponent* pLeft  = Internal::addWalker( manager, "Left", float3{ -1.5f, 0.05f, 0.0f } );
    CharacterControllerComponent* pRight = Internal::addWalker( manager, "Right", float3{ 1.5f, 0.05f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pLeft );
    SW_REQUIRE_NOT_NULL( pRight );
    manager.beginPlay();
    pLeft->setMoveVelocity( float3{ 2.0f, 0.0f, 0.0f } );
    pRight->setMoveVelocity( float3{ -2.0f, 0.0f, 0.0f } );
    for ( uint32 frame = 0; frame < 90; ++frame )
    {
        manager.update( Internal::kFrame );
        SW_REQUIRE_TRUE( pLeft->getSimulatedPosition()._x < pRight->getSimulatedPosition()._x );
    }
    const float32 gap = pRight->getSimulatedPosition()._x - pLeft->getSimulatedPosition()._x;
    SW_EXPECT_TRUE( gap > 2.0f * pLeft->getRadius() - 0.05f ); // 캡슐 둘이 겹치지 않았다
}

/**
 * @brief [PhysicsScene3DTest] 캐릭터가 트리거를 걸어 지나가면 트리거 바디에 시작 · 끝이 한 번씩 난다(감사 S-7 — 기본 속 바디)
 * @details 예전 기본(`_bInnerBody` 꺼짐)은 캐릭터가 월드에 바디가 없어 트리거에 잡히지 않았다(시작 0).
 */
SW_TEST_CASE( PhysicsScene3DTest, CharacterTriggersBeginAndEnd )
{
    using Internal = TestPhysicsScene3DInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::addFloor( manager ) );
    GameObject* pZone = manager.createGameObject( hashed_string( "Zone" ) );
    SW_REQUIRE_NOT_NULL( pZone );
    RigidBodyComponent* pTrigger = pZone->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pTrigger );
    PhysicsShapeDesc3D box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 0.5f, 1.0f, 2.0f };
    pTrigger->setShape( box );
    pTrigger->setBodyType( PhysicsBodyType::Static );
    pTrigger->setTrigger( true );
    pTrigger->setLayer( hashed_string( "Trigger" ) );
    pTrigger->setLocalPosition( float3{ 0.0f, 1.0f, 0.0f } );
    CharacterControllerComponent* pWalker = Internal::addWalker( manager, "Walker", float3{ -3.0f, 0.05f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pWalker );
    const ScenePhysics& physics = manager.getSubsystem<ScenePhysics>();
    manager.beginPlay();
    pWalker->setMoveVelocity( float3{ 3.0f, 0.0f, 0.0f } );
    uint32 begin = 0;
    uint32 end   = 0;
    for ( uint32 frame = 0; frame < 120; ++frame ) // 2 초 — x = -3 에서 x = 3 까지 트리거(x 는 -0.5 ~ 0.5)를 지나친다
    {
        manager.update( Internal::kFrame );
        begin += Internal::countPhase( physics.getFrameEvents3D(), pTrigger->getBodyHandle(), PhysicsContactPhase::Begin );
        end += Internal::countPhase( physics.getFrameEvents3D(), pTrigger->getBodyHandle(), PhysicsContactPhase::End );
    }
    SW_EXPECT_TRUE( pWalker->getSimulatedPosition()._x > 1.0f ); // 트리거에 막히지 않았다
    SW_EXPECT_EQUAL( 1u, begin );
    SW_EXPECT_EQUAL( 1u, end );
}
