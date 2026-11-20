// GimmickSensorOverlapTest — 기믹 센서가 트리거 계약(오브젝트 쌍 진입 · 이탈)대로 인원을 센다. 실제 2D 물리(Box2D 센서), 디바이스 없음(nogpu).
// 접속(폰 생성)과 접속 끊김(폰 파괴)을 서버 월드에서 하는 꼴이다 — 전용 서버(`Server --test`)도 GameFramework 를 실어 같은 케이스가 돈다.
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickSensorComponent.h"

using namespace sw;

namespace
{
    struct TestGimmickSensorOverlapInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        static RigidBody2DComponent* addBox( GameObject& object, const float3& localPosition, float32 halfExtent, PhysicsBodyType type, bool bTrigger )
        {
            RigidBody2DComponent* pBody = object.addComponent<RigidBody2DComponent>();
            PhysicsShapeDesc2D    box;
            box._type        = PhysicsShapeType2D::Box;
            box._halfExtents = float2{ halfExtent, halfExtent };
            pBody->setShape( box );
            pBody->setBodyType( type );
            pBody->setGravityFactor( 0.0f );
            pBody->setTrigger( bTrigger );
            pBody->setLocalPosition( localPosition );
            return pBody;
        }

        static void run( GameObjectManager& manager, uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                manager.update( kFrame );
            }
        }
    };
} // namespace

/**
 * @brief [GimmickSensorOverlapTest] 폰이 센서 안에서 생기면(접속) 인원 1, 폰의 감지 범위(트리거)가 꺼져도 몸이 안이면 1, 폰을 지우면(접속 끊김) 0
 * @details 예전에는 이벤트가 바디 쌍마다 와 몸이 여럿인 대상은 첫 이탈에서 빠졌고, 세지 않는 상대 트리거의 이탈도 몸을 뺐다.
 */
SW_TEST_CASE( GimmickSensorOverlapTest, PawnLoginRangeAndLogout )
{
    using Internal = TestGimmickSensorOverlapInternal;
    GameObjectManager manager;
    GameObject*       pPlate = manager.createGameObject( hashed_string( "Plate" ) );
    (void)Internal::addBox( *pPlate, float3{}, 2.0f, PhysicsBodyType::Static, true );
    GimmickSensorComponent* pSensor = pPlate->addComponent<GimmickSensorComponent>();
    SW_REQUIRE_NOT_NULL( pSensor );
    manager.beginPlay();
    Internal::run( manager, 2 );

    GameObject*           pPawn  = manager.createGameObject( hashed_string( "Pawn" ) ); // 접속 — 서버가 센서 안에 폰을 만든다
    RigidBody2DComponent* pBody  = Internal::addBox( *pPawn, float3{ 0.5f, 0.0f, 0.0f }, 0.25f, PhysicsBodyType::Dynamic, false );
    RigidBody2DComponent* pRange = Internal::addBox( *pPawn, float3{}, 0.75f, PhysicsBodyType::Dynamic, true ); // 감지 범위 — primary(몸)에 붙는다
    SW_REQUIRE_NOT_NULL( pBody );
    SW_REQUIRE_NOT_NULL( pRange );
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1, pSensor->getOccupantCount() );

    pRange->setActive( false ); // 범위만 꺼진다 — 몸은 안에 있다
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 1, pSensor->getOccupantCount() );

    manager.destroyGameObject( pPawn ); // 접속 끊김
    Internal::run( manager, 3 );
    SW_EXPECT_EQUAL( 0, pSensor->getOccupantCount() );
}
