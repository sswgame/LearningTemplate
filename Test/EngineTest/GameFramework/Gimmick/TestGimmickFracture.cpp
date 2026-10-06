// 기믹 × 파괴 — 폭발 드럼통의 폭발이 반경에 닿은 파괴 벽을 그 자리에서 깨고(사슬: 드럼통 → 드럼통 → 벽), 파괴 컴포넌트가 있는 엄폐물은 마지막 단계에서
// 몸을 끄는 대신 조각으로 부서진다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/DestructionTestUtil.h"

#include "GameFramework/Base/Gimmick/Genre/ShooterGimmicks.h"
#include "GameFramework/Base/Gimmick/GimmickSensorComponent.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct TestGimmickFractureInternal
    {
        /** @brief 반 크기 @p half 상자(바닥 가운데가 원점)를 벽돌로 쪼개 임시 `.fracture` 로 씁니다. */
        static sw::string writeBoxFracture( const utf8* pName, const sw::float3& half, uint32 columnCount, uint32 rowCount )
        {
            const sw::vector<sw::RHIVertex> listBox = test::DestructionTestUtil::makeBox( half, sw::float3{ 0.0f, half._y, 0.0f } );
            sw::FractureSettings            settings;
            settings._pattern          = sw::FracturePattern::Slices;
            settings._arrSliceCount[0] = columnCount;
            settings._arrSliceCount[1] = rowCount;
            settings._arrSliceCount[2] = 1;
            settings._sliceJitter      = 0.0f;
            settings._listLevelCount   = { 2 };
            sw::FractureAsset asset;
            sw::string        error;
            if ( sw::MeshFractureUtil::fracture( listBox, settings, asset, error ) == false )
                return {};
            const sw::string path = test::makeTempPath( pName );
            return asset.saveToFile( path ) ? path : sw::string{};
        }

        /** @brief 뿌리 정적 강체 · 메시 · 파괴 컴포넌트(바닥 앵커)를 가진 오브젝트입니다. */
        static sw::GameObject* spawnFractured( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& position, const sw::float3& half,
                                               const sw::string& fracturePath, sw::FractureAnchorMode anchorMode )
        {
            sw::GameObject*         pObject = manager.createGameObject( sw::hashed_string( pName ) );
            sw::RigidBodyComponent* pBody   = pObject->addComponent<sw::RigidBodyComponent>();
            sw::PhysicsShapeDesc3D  box;
            box._halfExtents   = half;
            box._localPosition = sw::float3{ 0.0f, half._y, 0.0f };
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Static );
            pBody->setLocalPosition( position );
            sw::MeshComponent*       pMesh = pObject->addComponent<sw::MeshComponent>();
            sw::shared_ptr<sw::Mesh> mesh  = sw::Mesh::create();
            mesh->setVertices( test::DestructionTestUtil::makeBox( half, sw::float3{ 0.0f, half._y, 0.0f } ) );
            pMesh->setMesh( mesh );
            sw::FractureComponent* pFracture = pObject->addComponent<sw::FractureComponent>();
            pFracture->setFracturePath( fracturePath );
            pFracture->setAnchorMode( anchorMode );
            return pObject;
        }

        static void tickFrames( sw::GameObjectManager& manager, int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
                manager.tick( 1.0f / 60.0f );
        }
    };
} // namespace

/**
 * @brief [GimmickFractureTest] 벽 앞 드럼통이 터져 벽을 그 자리에서 깨고 다음 걸음에 옆 드럼통이 사슬로 터져 또 맞히며, 반경 밖 벽은 온전하다
 */
SW_TEST_CASE( GimmickFractureTest, BarrelChainBreaksNearbyFracturedWall )
{
    using Internal                = TestGimmickFractureInternal;
    const sw::string fracturePath = Internal::writeBoxFracture( "gimmick_wall.fracture", sw::float3{ 2.0f, 1.5f, 0.15f }, 8, 6 );
    SW_ASSERT_FALSE( fracturePath.empty() );
    sw::GameObjectManager manager;
    sw::GameObject*       arrBarrel[2] = { manager.createGameObject( sw::hashed_string( "BarrelA" ) ), manager.createGameObject( sw::hashed_string( "BarrelB" ) ) };
    arrBarrel[0]->addComponent<sw::SceneComponent>()->setLocalPosition( sw::float3{ -1.0f, 0.5f, 3.2f } );
    arrBarrel[1]->addComponent<sw::SceneComponent>()->setLocalPosition( sw::float3{ 1.0f, 0.5f, 3.2f } );
    for ( sw::GameObject* pBarrel : arrBarrel )
    {
        pBarrel->addComponent<sw::GimmickSensorComponent>();
        pBarrel->addComponent<sw::ExplosiveBarrelComponent>();
    }
    sw::GameObject* pNearWall = Internal::spawnFractured( manager, "NearWall", sw::float3{ 0.0f, 0.0f, 1.5f }, sw::float3{ 2.0f, 1.5f, 0.15f }, fracturePath,
                                                          sw::FractureAnchorMode::Bottom );
    sw::GameObject* pFarWall  = Internal::spawnFractured( manager, "FarWall", sw::float3{ 40.0f, 0.0f, 0.0f }, sw::float3{ 2.0f, 1.5f, 0.15f }, fracturePath,
                                                          sw::FractureAnchorMode::Bottom );
    manager.beginPlay();
    Internal::tickFrames( manager, 3 );
    const sw::FractureComponent* pNear = pNearWall->getComponent<sw::FractureComponent>();
    const sw::FractureComponent* pFar  = pFarWall->getComponent<sw::FractureComponent>();
    arrBarrel[0]->getComponent<sw::GimmickSensorComponent>()->applyDamage( 40.0f );
    Internal::tickFrames( manager, 1 );
    SW_EXPECT_TRUE( arrBarrel[0]->getComponent<sw::ExplosiveBarrelComponent>()->hasExploded() );
    SW_EXPECT_FALSE( arrBarrel[1]->getComponent<sw::ExplosiveBarrelComponent>()->hasExploded() ); // 사슬은 다음 걸음
    Internal::tickFrames( manager, 6 );
    SW_EXPECT_TRUE( arrBarrel[1]->getComponent<sw::ExplosiveBarrelComponent>()->hasExploded() );
    SW_EXPECT_TRUE( pNear->isFractured() );
    // 두 폭발 모두 벽을 맞혔다 — 중심이 다른 폭발 사건 둘 이상(사슬로 터진 드럼통도).
    sw::vector<float32> listCenterX;
    for ( const sw::DestructionDamageEvent& event : pNear->getEventLog()._listEvent )
    {
        if ( event._kind != sw::DestructionDamageKind::Radial )
            continue;
        bool bKnown = false;
        for ( const float32 x : listCenterX )
            bKnown = bKnown || sw::MathUtil::abs( x - event._position._x ) < 0.5f;
        if ( bKnown == false )
            listCenterX.push_back( event._position._x );
    }
    SW_EXPECT_TRUE( listCenterX.size() >= 2 );
    SW_EXPECT_FALSE( pFar->isFractured() );
    SW_EXPECT_TRUE( pFar->getEventLog()._listEvent.empty() );
    manager.endPlay();
}

/**
 * @brief [GimmickFractureTest] 파괴 컴포넌트가 있는 엄폐물은 단계마다 조각을 깎고 마지막 단계에서 조각으로 부서진다(몸은 켜진 채 — 조각이 몸이다)
 */
SW_TEST_CASE( GimmickFractureTest, DestructibleCoverShattersInsteadOfHiding )
{
    using Internal                = TestGimmickFractureInternal;
    const sw::string fracturePath = Internal::writeBoxFracture( "gimmick_cover.fracture", sw::float3{ 1.0f, 0.6f, 0.4f }, 4, 3 );
    SW_ASSERT_FALSE( fracturePath.empty() );
    sw::GameObjectManager manager;
    sw::GameObject*       pCover = Internal::spawnFractured( manager, "Cover", sw::float3{}, sw::float3{ 1.0f, 0.6f, 0.4f }, fracturePath, sw::FractureAnchorMode::Bottom );
    pCover->addComponent<sw::GimmickSensorComponent>();
    sw::DestructibleComponent* pDestructible = pCover->addComponent<sw::DestructibleComponent>();
    manager.beginPlay();
    Internal::tickFrames( manager, 3 );
    pCover->getComponent<sw::GimmickSensorComponent>()->applyDamage( 60.0f );
    Internal::tickFrames( manager, 4 );
    SW_EXPECT_TRUE( pDestructible->isDestroyed() );
    const sw::FractureComponent* pFracture = pCover->getComponent<sw::FractureComponent>();
    SW_EXPECT_TRUE( pFracture->isFractured() );
    SW_EXPECT_TRUE( pFracture->getDebrisGroupCount() > 0 );
    SW_EXPECT_TRUE( pFracture->isActive() );
    manager.endPlay();
}
