// PhysicsScene3DTest — 3D 물리 씬의 용량 설정 · 임시 블록 폴백(감사 S-2), 캐릭터 속 바디 · 캐릭터끼리 막기(S-7), 캐릭터 일괄 이동 · 청크 결정성(S-8, PhysicsCharacterTest). 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/CollisionLayers.h"
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

        /** @brief 정적 상자를 놓습니다. */
        static RigidBodyComponent* addBox( GameObjectManager& manager, const utf8* pName, const float3& position, const float3& halfExtents )
        {
            GameObject*         pObject = manager.createGameObject( hashed_string( pName ) );
            RigidBodyComponent* pBody   = pObject != nullptr ? pObject->addComponent<RigidBodyComponent>() : nullptr;
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLocalPosition( position );
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

        /**
         * @brief 청크 @p chunkCount 개로 캐릭터 무리를 일괄 이동하며 월드를 스텝하고, 끝 자리(캐릭터마다 · 상자)를 냅니다.
         * @details 캐릭터 24 명이 둘레에서 가운데의 동적 상자로 걸어 서로 밀고 상자를 민다 — 캐릭터끼리 · 바디 쓰기가 모두 든다.
         */
        static bool runCrowd( uint32 chunkCount, vector<float3>& outListPosition, float3& outCratePosition )
        {
            constexpr uint32     kCharacterCount     = 8; // 24 명은 반지름 2.4 m 에서 서로 막혀 고리가 되고 상자에 닿지 못한다 — 8 명이면 약 0.8 m 까지 좁혀 상자(x 0 ~ 1)를 민다
            constexpr float32    kRingRadius         = 4.0f;
            constexpr float32    kWalkSpeed          = 2.0f;
            constexpr float32    kFallSpeed          = -2.0f; // 바닥에 붙어 있게 아래로도 민다
            constexpr uint32     kStepCount          = 180;   // 3 초 — 둘레(4 m)에서 상자까지 걸어 닿고 민다
            const PhysicsSystem& system              = engine::getSubsystem<PhysicsSystem>();
            PhysicsSettings      settings            = system.getSettings();
            settings._characterChunkCount            = chunkCount;
            const unique_ptr<IPhysicsScene3D> pScene = system.createScene3D( settings );
            if ( pScene == nullptr )
                return false;
            PhysicsBodyDesc3D floor;
            floor._listShape.push_back( PhysicsShapeDesc3D{} );
            floor._listShape[0]._type        = PhysicsShapeType3D::Box;
            floor._listShape[0]._halfExtents = float3{ 20.0f, 0.5f, 20.0f };
            floor._position                  = float3{ 0.0f, -0.5f, 0.0f };
            floor._type                      = PhysicsBodyType::Static;
            PhysicsBodyDesc3D crate          = floor;
            crate._listShape[0]._halfExtents = float3{ 0.5f, 0.5f, 0.5f };
            crate._position                  = float3{ 0.5f, 0.5f, 0.0f }; // 가운데에서 비켜 둔다 — 둘레의 미는 힘이 서로 지우지 않게
            crate._type                      = PhysicsBodyType::Dynamic;
            crate._mass                      = 20.0f;
            if ( pScene->createBody( floor ).isValid() == false )
                return false;
            const PhysicsBodyHandle crateBody = pScene->createBody( crate );
            if ( crateBody.isValid() == false )
                return false;
            vector<PhysicsCharacterMove3D> listMove;
            for ( uint32 index = 0; index < kCharacterCount; ++index )
            {
                const float32          angle = MathUtil::kTwoPi * static_cast<float32>( index ) / static_cast<float32>( kCharacterCount );
                const float3           direction{ MathUtil::cos( angle ), 0.0f, MathUtil::sin( angle ) };
                PhysicsCharacterDesc3D desc;
                desc._position                         = direction * kRingRadius + float3{ 0.0f, 0.05f, 0.0f };
                const PhysicsCharacterHandle character = pScene->createCharacter( desc );
                if ( character.isValid() == false )
                    return false;
                listMove.push_back( PhysicsCharacterMove3D{
                    character, direction * -kWalkSpeed + float3{ 0.0f, kFallSpeed, 0.0f }
                } );
            }
            for ( uint32 step = 0; step < kStepCount; ++step )
            {
                pScene->moveCharacters( listMove, kFrame );
                pScene->step( kFrame );
            }
            outListPosition.clear();
            for ( const PhysicsCharacterMove3D& move : listMove )
            {
                PhysicsCharacterState3D state{};
                if ( pScene->getCharacterState( move._character, state ) == false )
                    return false;
                outListPosition.push_back( state._position );
            }
            quaternion crateRotation{};
            return pScene->getBodyTransform( crateBody, outCratePosition, crateRotation );
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
        <_listChannel><CollisionChannelDef _name="WorldDynamic" /></_listChannel><_listProfile><CollisionProfileDef _name="Default" _objectChannel="WorldDynamic" /></_listProfile>
        <_listMaterial><PhysicsMaterialDef _name="Default" /></_listMaterial>
    </PhysicsSettings>)" ) );
    SW_EXPECT_TRUE( settings.loadFromXMLText( R"(<PhysicsSettings _maxBodyCount="1024" _maxBodyPairCount="1024" _maxContactCount="1024" _tempAllocatorBytes="65536" _maxDebrisBodyCount="64" _streamingFloorProbeDistance="1">
        <_listChannel><CollisionChannelDef _name="WorldDynamic" /></_listChannel><_listProfile><CollisionProfileDef _name="Default" _objectChannel="WorldDynamic" /></_listProfile>
        <_listMaterial><PhysicsMaterialDef _name="Default" /></_listMaterial>
        <_listGroundQueryLayer><item>Default</item></_listGroundQueryLayer>
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
 * @brief [PhysicsCharacterTest] 일괄 이동(`moveCharacters`)은 칸마다 `moveCharacter` 와 같은 결과이고, 청크 수(1 · 4 · 8)와 관계없이 비트까지 같다(감사 S-8)
 * @details 청크는 작업 풀이 나눠 돈다. 캐릭터끼리는 단계 앞 사본을 보고 바디에 주는 힘은 단계 끝에 목록 차례로 주므로, 서로 밀고 상자를 미는 무리도
 *          나누는 방법이 결과를 바꾸지 않는다(예전: 한 스레드로 차례로 돌았다).
 */
SW_TEST_CASE( PhysicsCharacterTest, BatchMoveEqualsOneByOne )
{
    vector<float3> listReference;
    float3         crateReference{};
    SW_REQUIRE_TRUE( TestPhysicsScene3DInternal::runCrowd( 1, listReference, crateReference ) );
    SW_EXPECT_TRUE( MathUtil::abs( crateReference._x - 0.5f ) + MathUtil::abs( crateReference._z ) > 0.01f ); // 무리가 상자를 밀었다(바디 쓰기 경로를 탔다)
    for ( const uint32 chunkCount : { 4u, 8u } )
    {
        vector<float3> listPosition;
        float3         cratePosition{};
        SW_REQUIRE_TRUE( TestPhysicsScene3DInternal::runCrowd( chunkCount, listPosition, cratePosition ) );
        SW_REQUIRE_EQUAL( listReference.size(), listPosition.size() );
        for ( size_t index = 0; index < listPosition.size(); ++index )
        {
            SW_EXPECT_TRUE( listPosition[index] == listReference[index] );
        }
        SW_EXPECT_TRUE( cratePosition == crateReference );
    }

    // 서로 닿지 않는 둘은 하나씩 옮긴 것과 같다.
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
 * @brief [PhysicsCharacterTest] 캐릭터끼리 막기의 격자는 순간이동 · 크기 바꾸기 · 지우기를 따라간다 — 옮긴 자리에서 막고 떠난 자리에서 막지 않는다
 * @details 상대는 격자에서 먼저 거른다(`JoltCharacterSnapshotCollision`). 격자가 사본을 따라가지 못하면 옛 셀에만 남아 새 자리의 캐릭터를 뚫고 지나간다.
 */
SW_TEST_CASE( PhysicsCharacterTest, SnapshotGridFollowsTeleportResizeAndDestroy )
{
    const PhysicsSystem&              system = engine::getSubsystem<PhysicsSystem>();
    const unique_ptr<IPhysicsScene3D> pScene = system.createScene3D();
    SW_REQUIRE_NOT_NULL( pScene.get() );
    PhysicsCharacterDesc3D       desc;
    const PhysicsCharacterHandle walker  = pScene->createCharacter( desc );
    desc._position                       = float3{ 50.0f, 0.0f, 0.0f };
    const PhysicsCharacterHandle blocker = pScene->createCharacter( desc );
    for ( uint32 index = 0; index < 64; ++index ) // 격자에 다른 셀들도 채운다
    {
        desc._position = float3{ -10.0f - 2.0f * static_cast<float32>( index % 8 ), 0.0f, 10.0f + 2.0f * static_cast<float32>( index / 8 ) };
        SW_REQUIRE_TRUE( pScene->createCharacter( desc ).isValid() );
    }

    const auto walk = [&pScene, walker]()
    {
        pScene->setCharacterPosition( walker, float3{} );
        for ( uint32 step = 0; step < 120; ++step )
        {
            (void)pScene->moveCharacter( walker, float3{ 2.0f, 0.0f, 0.0f }, TestPhysicsScene3DInternal::kFrame );
        }
        PhysicsCharacterState3D state{};
        SW_EXPECT_TRUE( pScene->getCharacterState( walker, state ) );
        return state._position._x;
    };
    constexpr float32 kTolerance = 0.1f;
    SW_EXPECT_TRUE( walk() > 3.5f ); // 상대는 멀다 — 4 m 를 걷는다

    pScene->setCharacterPosition( blocker, float3{ 2.5f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( walk() < 2.5f - 2.0f * desc._radius + kTolerance ); // 옮긴 자리에서 막는다

    SW_REQUIRE_TRUE( pScene->setCharacterSize( blocker, 1.0f, desc._halfHeight ) );
    SW_EXPECT_TRUE( walk() < 1.5f - desc._radius + kTolerance ); // 커진 몸이 더 앞에서 막는다

    pScene->setCharacterPosition( blocker, float3{ 50.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( walk() > 3.5f ); // 떠난 자리에서 막지 않는다

    pScene->setCharacterPosition( blocker, float3{ 2.5f, 0.0f, 0.0f } );
    pScene->destroyCharacter( blocker );
    SW_EXPECT_TRUE( walk() > 3.5f ); // 지운 캐릭터는 막지 않는다
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

/**
 * @brief [PhysicsCharacterTest] 캐릭터끼리 막기는 씬의 레이어 표를 본다 — 표가 막지 않는 두 레이어의 캐릭터는 마주 걸어 서로 지나친다
 * @details 예전에는 캐릭터 대 캐릭터가 레이어 표를 보지 않아 통과시킬 레이어(유령 · 같은 편 겹침)도 늘 막혔다. 변이: `CollideCharacter` 의 레이어 검사를 빼면 자리가 바뀌지 않는다.
 */
SW_TEST_CASE( PhysicsCharacterTest, CharactersOnNonCollidingLayersPassThrough )
{
    const PhysicsSystem&              system = engine::getSubsystem<PhysicsSystem>();
    const unique_ptr<IPhysicsScene3D> pScene = system.createScene3D();
    SW_REQUIRE_NOT_NULL( pScene.get() );
    CollisionLayers layers;
    layers.setLayerCollision( 1, 2, false );
    pScene->setLayerCollision( layers );
    PhysicsCharacterDesc3D desc;
    desc._layer                            = 1;
    desc._position                         = float3{ -1.5f, 0.0f, 0.0f };
    const PhysicsCharacterHandle left      = pScene->createCharacter( desc );
    desc._layer                            = 2;
    desc._position                         = float3{ 1.5f, 0.0f, 0.0f };
    const PhysicsCharacterHandle right     = pScene->createCharacter( desc );
    const PhysicsCharacterMove3D arrMove[] = {
        { left,  float3{ 2.0f, 0.0f, 0.0f }},
        {right, float3{ -2.0f, 0.0f, 0.0f }}
    };
    for ( uint32 step = 0; step < 90; ++step )
    {
        pScene->moveCharacters( arrMove, TestPhysicsScene3DInternal::kFrame );
    }
    PhysicsCharacterState3D leftState{};
    PhysicsCharacterState3D rightState{};
    SW_REQUIRE_TRUE( pScene->getCharacterState( left, leftState ) );
    SW_REQUIRE_TRUE( pScene->getCharacterState( right, rightState ) );
    SW_EXPECT_TRUE( leftState._position._x > rightState._position._x ); // 자리를 바꿨다
}

/**
 * @brief [PhysicsCharacterTest] 캡슐 키 줄이기는 다음 물리 프레임에 캐릭터를 다시 짓지 않고 판정 캡슐만 낮추고, 지은 반 높이 칸은 그대로 둔다
 * @details 예전에는 늪에 빠진 캐릭터의 캡슐이 지면에 남아 빠진 몸 위 허공도 맞았다(키를 바꾸려면 반 높이 칸을 고쳐 캐릭터를 다시 지어야 했다). 변이: `bResize` 를 빼면 반 높이가 그대로라 첫 단언이 진다.
 */
SW_TEST_CASE( PhysicsCharacterTest, HeightReductionShortensTheCapsuleInPlace )
{
    using Internal = TestPhysicsScene3DInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::addFloor( manager ) );
    CharacterControllerComponent* pWalker = Internal::addWalker( manager, "Sinker", float3{ 0.0f, 0.05f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pWalker );
    manager.beginPlay();
    manager.update( Internal::kFrame );
    const float32 authored = pWalker->getHalfHeight();
    const float32 before   = pWalker->getScaledCapsule()._halfHeight;

    pWalker->setHeightReduction( 0.4f );
    manager.update( Internal::kFrame );
    SW_EXPECT_NEAR( before - 0.2f, pWalker->getScaledCapsule()._halfHeight, 1.0e-4f );
    SW_EXPECT_EQUAL( authored, pWalker->getHalfHeight() ); // 지은 칸은 그대로

    pWalker->setHeightReduction( 0.0f );
    manager.update( Internal::kFrame );
    SW_EXPECT_NEAR( before, pWalker->getScaledCapsule()._halfHeight, 1.0e-4f );
}

/**
 * @brief [PhysicsCharacterTest] 걷는 동안 몸(회전)을 돌려도 걸은 만큼 간다 — 회전만 바뀐 트랜스폼을 옮긴 것으로 보지 않는다
 * @details 그린 자리는 두 스텝 사이라(스텝과 프레임이 같으면 지난 스텝 자리), 예전에는 몸을 돌리는 프레임마다 캐릭터가 그린 자리로 되돌아가 도는 동안 제자리였다.
 *          변이: `onPhysicsFrameBegin` 의 같은 자리 검사를 빼면 도는 쪽이 가지 못한다.
 */
SW_TEST_CASE( PhysicsCharacterTest, TurningDoesNotPullTheWalkerBack )
{
    using Internal = TestPhysicsScene3DInternal;
    float32 arrDistance[2]{};
    for ( uint32 run = 0; run < 2; ++run )
    {
        GameObjectManager manager;
        SW_REQUIRE_NOT_NULL( Internal::addFloor( manager ) );
        CharacterControllerComponent* pWalker = Internal::addWalker( manager, "Turner", float3{ 0.0f, 0.05f, 0.0f } );
        SW_REQUIRE_NOT_NULL( pWalker );
        manager.beginPlay();
        manager.update( Internal::kFrame );
        pWalker->setMoveVelocity( float3{ 0.0f, 0.0f, 3.0f } );
        for ( uint32 frame = 0; frame < 30; ++frame )
        {
            if ( run == 1 )
                pWalker->setLocalRotation( float3{ 0.0f, 0.05f * static_cast<float32>( frame + 1u ), 0.0f } );
            manager.update( Internal::kFrame );
        }
        arrDistance[run] = pWalker->getSimulatedPosition()._z;
    }
    SW_EXPECT_TRUE( arrDistance[0] > 1.0f );
    SW_EXPECT_NEAR( arrDistance[0], arrDistance[1], 1.0e-3f );
}

/**
 * @brief [PhysicsScene3DTest] 보간하는 동적 바디의 회전만 쓴 코드는 자리를, 자리만 쓴 코드는 회전을 되돌리지 않는다 — 쓰지 않은 몫은 시뮬레이션의 것이다
 * @details 그린 자세는 두 스텝 사이라(스텝과 프레임이 같으면 지난 스텝 자세) 예전에는 한 몫만 써도 통째로 바디에 옮겨, 회전을 쓰는 동안 바디가 허공에 멈췄다.
 */
SW_TEST_CASE( PhysicsScene3DTest, PartialTransformWritesKeepTheSimulatedRest )
{
    using Internal = TestPhysicsScene3DInternal;
    float3  arrFinalPosition[3]{};
    float32 arrFinalYaw[3]{};
    for ( uint32 run = 0; run < 3; ++run )
    {
        GameObjectManager   manager;
        RigidBodyComponent* pBody = manager.createGameObject( hashed_string( "Falling" ) )->addComponent<RigidBodyComponent>();
        PhysicsShapeDesc3D  ball;
        ball._type   = PhysicsShapeType3D::Sphere;
        ball._radius = 0.25f;
        pBody->setShape( ball );
        pBody->setBodyType( PhysicsBodyType::Dynamic );
        pBody->setLocalPosition( float3{ 0.0f, 50.0f, 0.0f } );
        manager.beginPlay();
        manager.update( Internal::kFrame );
        pBody->setAngularVelocity( float3{ 0.0f, 1.0f, 0.0f } );
        for ( uint32 frame = 0; frame < 30; ++frame )
        {
            if ( run == 1 )
                pBody->setLocalRotation( float3{ 0.0f, 0.05f * static_cast<float32>( frame + 1u ), 0.0f } );
            if ( run == 2 )
                pBody->setLocalPosition( pBody->getLocalPosition() + float3{ 0.1f, 0.0f, 0.0f } );
            manager.update( Internal::kFrame );
        }
        arrFinalPosition[run] = pBody->getWorldPosition();
        arrFinalYaw[run]      = pBody->getLocalRotation()._y;
    }
    SW_EXPECT_TRUE( arrFinalPosition[0]._y < 49.0f );                          // 반 초 떨어졌다
    SW_EXPECT_NEAR( arrFinalPosition[0]._y, arrFinalPosition[1]._y, 1.0e-3f ); // 회전만 썼다 — 그대로 떨어진다
    SW_EXPECT_TRUE( arrFinalPosition[2]._x > 2.0f );                           // 자리를 썼다
    SW_EXPECT_TRUE( arrFinalYaw[0] > 0.3f );                                   // 반 초 돌았다
    SW_EXPECT_NEAR( arrFinalYaw[0], arrFinalYaw[2], 1.0e-3f );                 // 자리만 썼다 — 그대로 돈다
}

/**
 * @brief [PhysicsCharacterTest] 캐릭터 속도는 실제로 난 속도다 — 서 있으면 0 이고 벽에 막히면 그쪽 몫이 0 이며, 내려 한 속도는 따로 읽는다
 * @details 예전 3D 는 Jolt 에 준 요청을 그대로 돌려줘 서 있어도 중력 한 스텝(0.1635 m/s), 벽에 막혀도 걷는 속도가 남았다(시나리오 localplayers 가 멈춤을 기다리다 졌다).
 */
SW_TEST_CASE( PhysicsCharacterTest, VelocityIsWhatActuallyHappened )
{
    using Internal = TestPhysicsScene3DInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::addFloor( manager ) );
    SW_REQUIRE_NOT_NULL( Internal::addBox( manager, "Wall", float3{ 2.0f, 1.0f, 0.0f }, float3{ 0.5f, 1.0f, 2.0f } ) );
    CharacterControllerComponent* pWalker = Internal::addWalker( manager, "Walker", float3{ 0.0f, 0.05f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pWalker );
    manager.beginPlay();
    for ( uint32 frame = 0; frame < 30; ++frame )
    {
        manager.update( Internal::kFrame );
    }
    SW_REQUIRE_TRUE( pWalker->isGrounded() );
    SW_EXPECT_NEAR( 0.0f, pWalker->getVelocity().getLength(), 0.01f );

    pWalker->setMoveVelocity( float3{ 2.0f, 0.0f, 0.0f } );
    for ( uint32 frame = 0; frame < 10; ++frame )
    {
        manager.update( Internal::kFrame );
    }
    SW_EXPECT_NEAR( 2.0f, pWalker->getVelocity()._x, 0.05f );
    for ( uint32 frame = 0; frame < 80; ++frame ) // 벽(x 1.5)까지 걸어 막힌다
    {
        manager.update( Internal::kFrame );
    }
    SW_EXPECT_NEAR( 0.0f, pWalker->getVelocity()._x, 0.05f );
    SW_EXPECT_NEAR( 2.0f, pWalker->getRequestedStepVelocity()._x, 1.0e-4f );
}

/** @brief [PhysicsCharacterTest] 천장에 머리를 부딪히면 오르던 속도가 끊겨 바로 떨어진다(2D 와 같다). 예전 3D 는 속도가 요청 그대로라 중력이 다 깎을 때까지 천장에 붙어 있었다 */
SW_TEST_CASE( PhysicsCharacterTest, CeilingStopsTheJump )
{
    using Internal = TestPhysicsScene3DInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::addFloor( manager ) );
    CharacterControllerComponent* pWalker = Internal::addWalker( manager, "Jumper", float3{ 0.0f, 0.05f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pWalker );
    const float32 height = 2.0f * ( pWalker->getHalfHeight() + pWalker->getRadius() );
    SW_REQUIRE_NOT_NULL( Internal::addBox( manager, "Ceiling", float3{ 0.0f, height + 0.2f + 0.5f, 0.0f }, float3{ 2.0f, 0.5f, 2.0f } ) ); // 머리 위 0.2 m
    manager.beginPlay();
    for ( uint32 frame = 0; frame < 30; ++frame )
    {
        manager.update( Internal::kFrame );
    }
    SW_REQUIRE_TRUE( pWalker->isGrounded() );
    pWalker->jump( 5.0f );
    manager.update( Internal::kFrame );
    SW_REQUIRE_FALSE( pWalker->isGrounded() );
    for ( uint32 frame = 0; frame < 29; ++frame ) // 반 초 — 끊기면 0.3 초 안에 디디고, 붙어 있으면 0.5 초에야 떨어지기 시작한다
    {
        manager.update( Internal::kFrame );
    }
    SW_EXPECT_TRUE( pWalker->isGrounded() );
}

/** @brief [PhysicsCharacterTest] 빈 자리 찾기 — 서 있는 캐릭터의 자리는 비지 않았고, 곁의 빈 자리를 준다. 제 몸은 막는 것으로 보지 않는다 */
SW_TEST_CASE( PhysicsCharacterTest, FreeSpotAvoidsStandingCharacters )
{
    using Internal = TestPhysicsScene3DInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::addFloor( manager ) );
    CharacterControllerComponent* pStanding = Internal::addWalker( manager, "Standing", float3{ 0.0f, 0.0f, 0.0f } );
    CharacterControllerComponent* pNew      = Internal::addWalker( manager, "New", float3{ 5.0f, 0.0f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pStanding );
    SW_REQUIRE_NOT_NULL( pNew );
    manager.beginPlay();
    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        manager.update( Internal::kFrame );
    }
    SW_EXPECT_TRUE( pStanding->isSpotFree( float3{} ) ); // 제 속 바디
    SW_EXPECT_FALSE( pNew->isSpotFree( float3{} ) );
    const sw::optional<float3> spot = pNew->findFreeSpot( float3{}, 3.0f );
    SW_REQUIRE_TRUE( spot.has_value() );
    SW_EXPECT_TRUE( float3::getHorizontalDistance( *spot, float3{} ) >= 2.0f * pNew->getRadius() );
    SW_EXPECT_FALSE( pNew->findFreeSpot( float3{}, 0.1f ).has_value() ); // 반경 안에 자리가 없다
}
