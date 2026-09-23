/**
 * @file TestObjectIdentity.cpp
 * @brief 같은 오브젝트를 되살릴 때 런타임 id 를 되살리는 장치 — `createGameObjectWithId` · `ObjectIdentity` · 프로세스 토큰.
 * @details 에디터 되돌리기 · 플레이 세션 복원 · 핫 리로드가 이것에 기대어 `GameObjectHandle` · `ComponentHandle` 을 이어 간다.
 *          각 경로의 배선(트랜잭션 · 선택)은 EditorTest 가 보고, 여기서는 그 아래 장치를 직접 본다.
 */
#include "pch.h"

#include "Core/Container/vector.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [ObjectIdentityTest] 원래 id 로 다시 만들고, 그 id 가 아직 등록돼 있으면 새 id 로 물러선다
 * @details 물러서는 이유: 옛 오브젝트가 삭제 대기인 채로 같은 id 를 쓰면, 나중에 옛 것의 지연 파괴가 id 로 정리하는
 *          항목(슬롯 표 · 에디터 GUID 맵)을 새 것 몫까지 지운다.
 */

SW_TEST_CASE( ObjectIdentityTest, CreateWithIdReusesFreedIdOnly )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pOriginal = manager.createGameObject( sw::hashed_string( "Original" ) );
    SW_ASSERT_NOT_NULL( pOriginal );
    const uint64 originalId = pOriginal->getObjectId();

    BLOCK( "아직 등록돼 있으면 새 id" )
    {
        sw::GameObject* pClash = manager.createGameObjectWithId( sw::hashed_string( "Clash" ), originalId );
        SW_ASSERT_NOT_NULL( pClash );
        SW_EXPECT_TRUE( pClash->getObjectId() != originalId );
    }

    BLOCK( "삭제 대기도 등록돼 있는 것이다 — 지연 파괴가 끝나기 전에는 새 id" )
    {
        manager.destroyObject( pOriginal );
        sw::GameObject* pEarly = manager.createGameObjectWithId( sw::hashed_string( "Early" ), originalId );
        SW_ASSERT_NOT_NULL( pEarly );
        SW_EXPECT_TRUE( pEarly->getObjectId() != originalId );
    }

    BLOCK( "지연 파괴가 끝나면 원래 id 로 되살린다" )
    {
        manager.processDeferredDestruction();
        sw::GameObject* pRestored = manager.createGameObjectWithId( sw::hashed_string( "Original" ), originalId );
        SW_ASSERT_NOT_NULL( pRestored );
        SW_EXPECT_EQUAL( originalId, pRestored->getObjectId() );
        SW_EXPECT_TRUE( manager.resolveGameObject( pRestored->getHandle() ) == pRestored );
    }

    BLOCK( "발급 카운터는 되살린 id 뒤로 밀린다 — 앞으로의 발급과 겹치지 않는다" )
    {
        constexpr uint64 kFarId = 100000;
        sw::GameObject*  pFar   = manager.createGameObjectWithId( sw::hashed_string( "Far" ), kFarId );
        SW_ASSERT_NOT_NULL( pFar );
        SW_EXPECT_EQUAL( kFarId, pFar->getObjectId() );

        sw::GameObject* pNext = manager.createGameObject( sw::hashed_string( "Next" ) );
        SW_ASSERT_NOT_NULL( pNext );
        SW_EXPECT_TRUE( pNext->getObjectId() > kFarId );
    }
}

/**
 * @brief [ObjectIdentityTest] id 와 함께 읽으면 다시 만든 컴포넌트가 원래 componentId 를 받는다
 * @details 로드는 컴포넌트를 전부 지우고 팩토리로 새로 만든다. id 를 같이 넘기지 않으면(씬 · 프리팹 로드와 복제가 가는 길)
 *          새 id 를 받는다 — 마지막 블록이 그 대조군이다.
 */
SW_TEST_CASE( ObjectIdentityTest, BinaryLoadRestoresComponentIds )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pSource = manager.createGameObject( sw::hashed_string( "Source" ) );
    SW_ASSERT_NOT_NULL( pSource );
    sw::SceneComponent* pSceneComp = pSource->addComponent<sw::SceneComponent>();
    sw::MeshComponent*  pMesh      = pSource->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );
    SW_ASSERT_NOT_NULL( pMesh );
    manager.mergePendingAdds();

    const sw::ObjectIdentity identity = sw::ObjectStateSerializer::captureIdentity( pSource );
    SW_ASSERT_EQUAL( size_t( 2 ), identity._listComponent.size() );
    const sw::ComponentHandle sceneHandle = pSceneComp->getHandle();
    const sw::ComponentHandle meshHandle  = pMesh->getHandle();

    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pSource, bytes ) );

    // 같은 오브젝트를 되살리는 길 — 지우고, 같은 id 로 만들고, id 와 함께 읽는다(핫 리로드 · 되돌리기와 같은 순서).
    manager.destroyObject( pSource );
    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( manager.resolveComponent( sceneHandle ) == nullptr );

    sw::GameObject* pRestored = manager.createGameObjectWithId( sw::hashed_string( "Source" ), identity._objectId );
    SW_ASSERT_NOT_NULL( pRestored );
    sw::string parentName;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromBinaryBuffer( pRestored, bytes.data(), bytes.size(), parentName, &identity ) > 0 );

    sw::Component* pRestoredScene = manager.resolveComponent( sceneHandle );
    sw::Component* pRestoredMesh  = manager.resolveComponent( meshHandle );
    SW_ASSERT_NOT_NULL( pRestoredScene );
    SW_ASSERT_NOT_NULL( pRestoredMesh );
    SW_EXPECT_TRUE( pRestoredScene != pRestoredMesh );
    SW_EXPECT_TRUE( pRestoredScene->isSceneComponent() );
    SW_EXPECT_TRUE( pRestoredMesh->getOwner() == pRestored );

    BLOCK( "대조군 — id 없이 읽으면 새 componentId 를 받는다" )
    {
        sw::GameObject* pCopy = manager.createGameObject( sw::hashed_string( "Copy" ) );
        SW_ASSERT_NOT_NULL( pCopy );
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromBinaryBuffer( pCopy, bytes.data(), bytes.size(), parentName ) > 0 );

        uint32 sameIdCount = 0;
        for ( const sw::Component* pComp : pCopy->getComponents() )
        {
            if ( pComp == nullptr )
                continue;
            for ( const sw::ObjectIdentity::ComponentEntry& entry : identity._listComponent )
                sameIdCount += ( pComp->getComponentId() == entry._componentId ) ? 1u : 0u;
        }
        SW_EXPECT_EQUAL( 0u, sameIdCount );
    }
}

/**
 * @brief [ObjectIdentityTest] id 목록은 바이너리로 왕복하고, 망가진 입력은 0 바이트로 거절한다
 */
SW_TEST_CASE( ObjectIdentityTest, WireFormatRoundTripAndRejectsGarbage )
{
    sw::ObjectIdentity identity;
    identity._objectId = 42;
    identity._listComponent.push_back( sw::ObjectIdentity::ComponentEntry{ sw::hashed_string( "SceneComponent" ), 7 } );
    identity._listComponent.push_back( sw::ObjectIdentity::ComponentEntry{ sw::hashed_string( "MeshComponent" ), 9 } );

    sw::vector<uint8> bytes;
    sw::ObjectStateSerializer::writeIdentity( identity, bytes );

    sw::ObjectIdentity readBack;
    SW_ASSERT_EQUAL( bytes.size(), sw::ObjectStateSerializer::readIdentity( bytes.data(), bytes.size(), readBack ) );
    SW_EXPECT_EQUAL( uint64( 42 ), readBack._objectId );
    SW_ASSERT_EQUAL( size_t( 2 ), readBack._listComponent.size() );
    SW_EXPECT_TRUE( readBack._listComponent[1]._typeName == sw::hashed_string( "MeshComponent" ) );
    SW_EXPECT_EQUAL( uint64( 9 ), readBack._listComponent[1]._componentId );

    // 잘린 입력
    SW_EXPECT_EQUAL( size_t( 0 ), sw::ObjectStateSerializer::readIdentity( bytes.data(), bytes.size() - 1, readBack ) );

    // 남은 바이트로 담을 수 없는 개수 — `reserve` 로 가기 전에 거절해야 한다.
    sw::vector<uint8> bogus( bytes.begin(), bytes.begin() + sizeof( uint64 ) );
    const uint32      hugeCount = 0x7FFFFFFFu;
    const uint8*      pCount    = reinterpret_cast<const uint8*>( &hugeCount );
    bogus.insert( bogus.end(), pCount, pCount + sizeof( hugeCount ) );
    SW_EXPECT_EQUAL( size_t( 0 ), sw::ObjectStateSerializer::readIdentity( bogus.data(), bogus.size(), readBack ) );
}

/**
 * @brief [ObjectIdentityTest] 프로세스 토큰은 0 이 아니고, 한 프로세스 안에서 바뀌지 않는다
 * @details 핫 리로드 스냅샷(같은 프로세스)과 세이브 파일(다른 실행)을 가르는 값이다. 0 이면 토큰이 없던 옛 봉투와 구분되지 않는다.
 */
SW_TEST_CASE( ObjectIdentityTest, ProcessTokenIsStableAndNonZero )
{
    const uint64 token = sw::ObjectStateSerializer::getProcessToken();
    SW_EXPECT_TRUE( token != 0 );
    SW_EXPECT_EQUAL( token, sw::ObjectStateSerializer::getProcessToken() );
}
