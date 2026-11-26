// HitDecalTest — 맞음 데칼의 자리: 때린 방향 뒤의 정적 면이 먼저이고, 없으면 발밑 면이며, 움직이는 것 위에는 세우지 않는다. 데칼 상자의 위 축은 면의 법선이다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Character/Combat/HitDecalComponent.h"

using namespace sw;

namespace
{
    struct HitDecalTestUtil
    {
        static constexpr float32 kReach = 3.0f;

        static GameObject* addBox( GameObjectManager& manager, const utf8* pName, const float3& position, const float3& halfExtents, PhysicsBodyType type )
        {
            GameObject*         pObject = manager.createGameObject( hashed_string( pName ) );
            RigidBodyComponent* pBody   = pObject != nullptr ? pObject->addComponent<RigidBodyComponent>() : nullptr;
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( type );
            pBody->setLocalPosition( position );
            return pObject;
        }

        static HitInfo makeHit( const float3& direction )
        {
            HitInfo hit;
            hit._point     = float3{ 0.0f, 1.0f, 0.0f };
            hit._direction = direction;
            hit._damage    = 10.0f;
            return hit;
        }
    };
} // namespace

/** @brief [HitDecalTest] 뒤 벽이 닿으면 벽에, 닿지 않으면 땅에 놓이고, 움직이는 상자 앞에서는 땅으로 떨어진다 */
SW_TEST_CASE( HitDecalTest, SurfaceBehindThenBelow )
{
    using Util = HitDecalTestUtil;
    GameObjectManager manager;
    GameObject*       pGround = Util::addBox( manager, "Ground", float3{ 0.0f, -0.5f, 0.0f }, float3{ 20.0f, 0.5f, 20.0f }, PhysicsBodyType::Static );
    GameObject*       pWall   = Util::addBox( manager, "Wall", float3{ 0.0f, 1.0f, 2.5f }, float3{ 5.0f, 2.0f, 0.5f }, PhysicsBodyType::Static );
    GameObject*       pCrate  = Util::addBox( manager, "Crate", float3{ 2.0f, 1.0f, 0.0f }, float3{ 0.5f, 0.5f, 0.5f }, PhysicsBodyType::Kinematic );
    SW_REQUIRE_NOT_NULL( pGround );
    SW_REQUIRE_NOT_NULL( pWall );
    SW_REQUIRE_NOT_NULL( pCrate );
    manager.beginPlay();
    manager.update( 1.0f / 60.0f ); // 바디는 첫 물리 프레임에 생긴다

    CharacterRayHit surface;
    SW_REQUIRE_TRUE( HitDecalComponent::findSurface( manager, Util::makeHit( float3{ 0.0f, 0.0f, 1.0f } ), Util::kReach, 0, surface ) );
    SW_EXPECT_TRUE( surface._pObject == pWall );
    SW_EXPECT_NEAR( -1.0f, surface._normal._z, 1.0e-3f );
    SW_REQUIRE_TRUE( HitDecalComponent::findSurface( manager, Util::makeHit( float3{ 0.0f, 0.0f, -1.0f } ), Util::kReach, 0, surface ) );
    SW_EXPECT_TRUE( surface._pObject == pGround );
    SW_REQUIRE_TRUE( HitDecalComponent::findSurface( manager, Util::makeHit( float3{ 1.0f, 0.0f, 0.0f } ), Util::kReach, 0, surface ) );
    SW_EXPECT_TRUE( surface._pObject == pGround ); // 움직이는 상자는 건너뛴다
    HitInfo planar = Util::makeHit( float3{ 0.0f, 0.0f, 1.0f } );
    planar._bIs2D  = true;
    SW_EXPECT_FALSE( HitDecalComponent::findSurface( manager, planar, Util::kReach, 0, surface ) );
}

/** @brief [HitDecalTest] 데칼 상자의 위 축은 법선 × 깊이이고, 나머지 두 축은 법선에 수직인 크기 길이다 */
SW_TEST_CASE( HitDecalTest, DecalBoxStandsOnTheNormal )
{
    const float3   normal{ 0.0f, 0.0f, -1.0f };
    const float4x4 world = HitDecalComponent::computeDecalWorld( float3{ 1.0f, 2.0f, 3.0f }, normal, 0.7f, 2.0f, 0.5f );
    const float3   right{ world._11, world._12, world._13 };
    const float3   up{ world._21, world._22, world._23 };
    const float3   forward{ world._31, world._32, world._33 };
    SW_EXPECT_NEAR( -0.5f, up._z, 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f, right.getLength(), 1.0e-4f );
    SW_EXPECT_NEAR( 2.0f, forward.getLength(), 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, right.dot( normal ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, forward.dot( normal ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, right.dot( forward ), 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, world.getTranslation()._x, 1.0e-6f );
}
