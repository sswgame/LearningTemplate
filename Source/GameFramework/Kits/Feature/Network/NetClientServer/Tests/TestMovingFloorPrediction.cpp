// MovingFloorPredictionTest — 틱 함수 바닥(왕복 발판 · 승강기 · 회전판) 위의 이동 예측: 명령마다 바닥을 그 틱에 두면 지연 · 손실에서도 보정이 없고, 탄 채로 실려 가며, 다시 돌려도 같다.
// 실제 3D 물리(Jolt), 디바이스 · 소켓 없음(nogpu — 망은 틱 수의 줄이다).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ClientPrediction.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/InterpToMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/TickedFloorStep.h"

#include <utility>

using namespace sw;

namespace
{
    enum class FloorTestKind : uint8
    {
        PingPong = 0, ///< 2 초에 2 m 갔다 온다
        Lift,         ///< 2 초에 2 m 오르내린다
        Rotation      ///< 초당 0.5 라디안 돈다
    };

    /** @brief 입력 하나와 그 틱입니다. */
    struct FloorTestInput
    {
        ControlIntent _intent{};
        uint32        _tick{ 0 };
    };

    /** @brief 땅 · 움직이는 바닥 하나 · 네트가 움직이는 폰 하나인 월드입니다. 폰은 바닥 위(회전판은 가운데에서 1 m)에 선다. */
    struct FloorTestWorld
    {
        static constexpr float32 kTick            = 1.0f / 30.0f;
        static constexpr int64   kFloorTickOffset = 1000; ///< 서버가 정한 바닥 틱 − 입력 틱

        GameObjectManager           _manager{};
        InterpToMovementComponent*  _pFloor{ nullptr };
        CharacterMovementComponent* _pMovement{ nullptr };
        CharacterMoveState          _state{};

        void build( FloorTestKind kind )
        {
            RigidBodyComponent* pGround = _manager.createGameObject( hashed_string( "Ground" ) )->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = float3{ 50.0f, 0.5f, 50.0f };
            pGround->setShape( box );
            pGround->setBodyType( PhysicsBodyType::Static );
            pGround->setLocalPosition( float3{ 0.0f, -0.5f, 0.0f } );

            GameObject*         pFloorObject = _manager.createGameObject( hashed_string( "Floor" ) );
            RigidBodyComponent* pFloorBody   = pFloorObject->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  slab;
            slab._type        = PhysicsShapeType3D::Box;
            slab._halfExtents = float3{ 3.0f, 0.1f, 3.0f };
            pFloorBody->setShape( slab );
            pFloorBody->setBodyType( PhysicsBodyType::Kinematic );
            pFloorBody->setLocalPosition( float3{ 0.0f, 0.2f, 0.0f } );
            _pFloor               = pFloorObject->addComponent<InterpToMovementComponent>();
            const float3 waypoint = kind == FloorTestKind::Lift ? float3{ 0.0f, 2.0f, 0.0f } : float3{ 2.0f, 0.0f, 0.0f };
            if ( kind == FloorTestKind::Rotation )
                _pFloor->setTurnRate( Radians::fromRadians( 0.5f ) );
            else
                _pFloor->setPath( vector<float3>{ waypoint }, 2.0f, true );

            GameObject* pWalker = _manager.createGameObject( hashed_string( "Walker" ) );
            pWalker->addComponent<CharacterControllerComponent>()->setLocalPosition( float3{ kind == FloorTestKind::Rotation ? 1.0f : 0.0f, 0.35f, 0.0f } );
            pWalker->addComponent<PawnComponent>()->setAutoPossess( PawnAutoPossess::None );
            _pMovement = pWalker->addComponent<CharacterMovementComponent>();
            _manager.beginPlay();
            _manager.update( 1.0f / 60.0f ); // 첫 물리 프레임이 캐릭터 · 바디를 짓는다
            _pMovement->setNetDriven( true );
            _state = _pMovement->getState();
            // 바닥의 첫 틱 자세 위에 세운다 — 시작 자리는 시험의 틱(입력 0 + 차이)에서 잰다.
            float3     position;
            quaternion rotation;
            SW_REQUIRE_TRUE( _pFloor->computePose( static_cast<float64>( kFloorTickOffset ) * static_cast<float64>( kTick ), position, rotation ) );
            const float3 local{ kind == FloorTestKind::Rotation ? 1.0f : 0.0f, 0.1f, 0.0f };
            _state._motion._position = position + float3::transform( local, rotation );
        }

        CharacterMoveState simulate( const CharacterMoveState& state, const FloorTestInput& input, CharacterMotionPass pass )
        {
            TickedFloorStep floorStep{ _manager, kTick };
            const int64     floorTick = static_cast<int64>( input._tick ) + kFloorTickOffset;
            floorStep.beginTick( floorTick );
            const CharacterMoveState next = _pMovement->simulate( state, input._intent, kTick, pass );
            floorStep.endTick( floorTick );
            _pMovement->setState( next ); // 접촉은 새 자세의 바닥에서 찾는다
            return next;
        }

        void step( const FloorTestInput& input ) { _state = simulate( _state, input, CharacterMotionPass::Live ); }

        /** @brief 틱 @p tick 의 바닥 기준 자리입니다. */
        float3 computeFloorLocal( uint32 tick ) const
        {
            float3     position;
            quaternion rotation;
            if ( _pFloor->computePose( static_cast<float64>( static_cast<int64>( tick ) + kFloorTickOffset ) * static_cast<float64>( kTick ), position, rotation ) == false )
                return float3{};
            return float3::transform( _state._motion._position - position, std::as_const( rotation ).inverse() );
        }
    };

    /** @brief 서버가 처리한 입력 틱과 그 끝 상태 하나입니다(망의 줄). */
    struct FloorOwnerState
    {
        CharacterMoveState _state{};
        uint32             _inputTick{ 0 };
        uint32             _arrivalTick{ 0 };
    };

    FloorTestInput makeInput( uint32 tick, uint32 walkFromTick )
    {
        FloorTestInput input;
        input._tick = tick;
        if ( tick >= walkFromTick )
            input._intent._move = float2{ 0.0f, 1.0f };
        input._intent.quantize();
        return input;
    }

    /** @brief 지연 네 틱(약 130 ms) · 상태 넷 중 하나 손실 · 입력 셋씩 몰려 오는 서버에서 @p tickCount 틱을 돌립니다. @p walkFromTick 부터 앞으로 걷는다. */
    void runOnFloor( FloorTestKind kind, uint32 tickCount, uint32 walkFromTick, uint32& outCorrectionCount, float32& outError, float32& outSlip )
    {
        constexpr uint32 kDelayTicks = 4;
        FloorTestWorld   server;
        FloorTestWorld   client;
        server.build( kind );
        client.build( kind );
        ClientPrediction<CharacterMoveState, FloorTestInput> prediction;
        vector<FloorOwnerState>                              listInFlight;
        uint32                                               serverTick = 0;
        float3                                               firstLocal{};
        outSlip = 0.0f;
        for ( uint32 tick = 1; tick <= tickCount; ++tick )
        {
            client.step( makeInput( tick, walkFromTick ) );
            SW_REQUIRE_TRUE( prediction.record( tick, makeInput( tick, walkFromTick ), client._state ) );
            if ( tick == 10 )
                firstLocal = client.computeFloorLocal( tick );
            if ( tick > 10 && tick < walkFromTick )
                outSlip = MathUtil::max( outSlip, float3::getDistance( firstLocal, client.computeFloorLocal( tick ) ) );
            // 서버는 지연 뒤에, 세 틱치를 한 프레임에 몰아 받는다 — 바닥은 명령마다 그 명령의 틱이다.
            while ( tick > kDelayTicks && tick % 3u == 0u && serverTick < tick - kDelayTicks )
            {
                ++serverTick;
                server.step( makeInput( serverTick, walkFromTick ) );
                if ( serverTick % 4u != 0u )
                    listInFlight.push_back( FloorOwnerState{ server._state, serverTick, tick + kDelayTicks } );
            }
            while ( listInFlight.empty() == false && listInFlight.front()._arrivalTick <= tick )
            {
                const FloorOwnerState           owner  = listInFlight.front();
                const PredictionReconcileResult result = prediction.reconcile(
                    owner._inputTick, owner._state, client._state,
                    [&client]( const CharacterMoveState& state, const FloorTestInput& input )
                { return client.simulate( state, input, CharacterMotionPass::Replay ); },
                    []( const CharacterMoveState& left, const CharacterMoveState& right )
                { return ( left._motion._position - right._motion._position ).getLength() <= 0.01f; } );
                SW_EXPECT_TRUE( result == PredictionReconcileResult::Matched || result == PredictionReconcileResult::Corrected );
                listInFlight.erase( listInFlight.begin() );
            }
        }
        while ( serverTick < tickCount )
        {
            ++serverTick;
            server.step( makeInput( serverTick, walkFromTick ) );
        }
        outCorrectionCount = prediction.getCorrectionCount();
        outError           = ( server._state._motion._position - client._state._motion._position ).getLength();
    }
} // namespace

/** @brief [MovingFloorPredictionTest] 왕복 발판 · 승강기 · 회전판 위에 서 있으면 보정이 없고 바닥과 함께 실려 간다 — 입력이 몰려 와도 같다 */
SW_TEST_CASE( MovingFloorPredictionTest, StandingRiderIsCarriedWithoutCorrections )
{
    constexpr FloorTestKind kArrKind[] = { FloorTestKind::PingPong, FloorTestKind::Lift, FloorTestKind::Rotation };
    for ( const FloorTestKind kind : kArrKind )
    {
        uint32  correctionCount = 0;
        float32 error           = 0.0f;
        float32 slip            = 0.0f;
        runOnFloor( kind, 150, 1000, correctionCount, error, slip );
        SW_EXPECT_EQUAL( 0u, correctionCount );
        SW_EXPECT_TRUE( error <= 0.01f );
        SW_EXPECT_TRUE( slip <= 0.15f ); // 회전판은 걸음마다 접선으로 가 조금 밀린다
    }
}

/** @brief [MovingFloorPredictionTest] 바닥 위를 걷다가 내려와 땅으로 옮겨도 예측이 맞는다 */
SW_TEST_CASE( MovingFloorPredictionTest, WalkingOffTheFloorStaysPredicted )
{
    constexpr FloorTestKind kArrKind[] = { FloorTestKind::PingPong, FloorTestKind::Rotation };
    for ( const FloorTestKind kind : kArrKind )
    {
        uint32  correctionCount = 0;
        float32 error           = 0.0f;
        float32 slip            = 0.0f;
        runOnFloor( kind, 150, 60, correctionCount, error, slip ); // 3 초 걸으면 바닥 끝(3 m)을 넘어 땅이다
        SW_EXPECT_EQUAL( 0u, correctionCount );
        SW_EXPECT_TRUE( error <= 0.01f );
    }
}

/** @brief [MovingFloorPredictionTest] 장치가 사라지면 바디의 자세 · 속도가 되돌아온다 — 물리 프레임은 이 장치를 모른다 */
SW_TEST_CASE( MovingFloorPredictionTest, BodiesReturnAfterTheNetStep )
{
    FloorTestWorld world;
    world.build( FloorTestKind::PingPong );
    const RigidBodyComponent* pBody  = world._pFloor->getOwner()->getComponent<RigidBodyComponent>();
    const float3              before = pBody->getWorldPosition();
    world.step( makeInput( 1, 1000 ) );
    const float3 local = world.computeFloorLocal( 1 );
    SW_EXPECT_NEAR( 0.0f, local._x, 0.05f ); // 틱 1001 의 바닥 위다 — 트랜스폼의 바닥이 아니다
    SW_EXPECT_TRUE( float3::getDistance( before, pBody->getWorldPosition() ) < 1.0e-4f );
    {
        TickedFloorStep floorStep{ world._manager, FloorTestWorld::kTick };
        floorStep.beginTick( 5 );
        floorStep.endTick( 5 );
    }
    world._manager.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( float3::getDistance( before, pBody->getWorldPosition() ) < 0.1f );
}
