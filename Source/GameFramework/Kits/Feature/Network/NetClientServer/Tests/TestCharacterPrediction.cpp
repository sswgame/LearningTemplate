// CharacterPredictionTest — 캐릭터 이동을 공용 예측 장치(`ClientPrediction`)로 돌린다: 지연 · 손실에서 예측이 맞으면 보정이 없고, 어긋나면 서버 상태로 되돌아온다.
// 실제 3D 물리(Jolt), 디바이스 · 소켓 없음(nogpu — 망은 틱 수의 줄이다).
#include "pch.h"

#include "Core/Container/vector.h"
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

using namespace sw;

namespace
{
    /** @brief 바닥과 네트가 움직이는 폰 하나인 월드입니다. */
    struct PredictionTestWorld
    {
        static constexpr float32 kTick = 1.0f / 30.0f;

        GameObjectManager           _manager{};
        CharacterMovementComponent* _pMovement{ nullptr };
        CharacterMoveState          _state{};

        void build()
        {
            RigidBodyComponent* pFloor = _manager.createGameObject( hashed_string( "Floor" ) )->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = float3{ 50.0f, 0.5f, 50.0f };
            pFloor->setShape( box );
            pFloor->setBodyType( PhysicsBodyType::Static );
            pFloor->setLocalPosition( float3{ 0.0f, -0.5f, 0.0f } );
            GameObject* pWalker = _manager.createGameObject( hashed_string( "Walker" ) );
            pWalker->addComponent<CharacterControllerComponent>()->setLocalPosition( float3{ 0.0f, 0.05f, 0.0f } );
            pWalker->addComponent<PawnComponent>()->setAutoPossess( PawnAutoPossess::None );
            _pMovement = pWalker->addComponent<CharacterMovementComponent>();
            _manager.beginPlay();
            _manager.update( 1.0f / 60.0f ); // 첫 물리 프레임이 캐릭터 · 바디를 짓는다
            _pMovement->setNetDriven( true );
            _state = _pMovement->getState();
        }

        void step( const ControlIntent& intent ) { _state = _pMovement->simulate( _state, intent, kTick, CharacterMotionPass::Live ); }
    };

    /** @brief 서버가 처리한 입력 틱과 그 끝 상태 하나입니다(망의 줄). */
    struct OwnerStateInFlight
    {
        CharacterMoveState _state{};
        uint32             _inputTick{ 0 };
        uint32             _arrivalTick{ 0 };
    };

    ControlIntent makeIntent( uint32 tick )
    {
        ControlIntent intent;
        intent._move       = tick % 40u < 30u ? float2{ 0.0f, 1.0f } : float2{ 1.0f, 0.0f };
        intent._controlYaw = static_cast<float32>( tick ) * 0.01f;
        intent.quantize();
        return intent;
    }

    /** @brief 지연 @p kDelayTicks · 상태 넷 중 하나 손실에서 @p tickCount 틱을 돌리고, 틱 @p teleportTick 에 클라이언트 자리를 2 m 민다(0 이면 밀지 않는다). 보정 횟수와 마지막 어긋남을 냅니다. */
    void runPrediction( uint32 tickCount, uint32 teleportTick, uint32& outCorrectionCount, float32& outError )
    {
        constexpr uint32    kDelayTicks = 4;
        PredictionTestWorld server;
        PredictionTestWorld client;
        server.build();
        client.build();
        ClientPrediction<CharacterMoveState, ControlIntent> prediction;
        vector<OwnerStateInFlight>                          listInFlight;
        vector<CharacterMoveState>                          listServerState( 1, server._state );
        for ( uint32 tick = 1; tick <= tickCount; ++tick )
        {
            if ( tick == teleportTick )
                client._state._motion._position += float3{ 2.0f, 0.0f, 0.0f }; // 클라이언트만 옮겼다(속도 치트 · 어긋난 예측)
            client.step( makeIntent( tick ) );
            SW_REQUIRE_TRUE( prediction.record( tick, makeIntent( tick ), client._state ) );
            // 서버는 지연 뒤에 그 입력을 받는다(입력은 다시 보내므로 잃지 않는다).
            if ( tick > kDelayTicks )
            {
                const uint32 inputTick = tick - kDelayTicks;
                server.step( makeIntent( inputTick ) );
                listServerState.push_back( server._state );
                if ( inputTick % 4u != 0u ) // 소유자 상태는 비신뢰라 넷 중 하나를 잃는다
                    listInFlight.push_back( OwnerStateInFlight{ server._state, inputTick, tick + kDelayTicks } );
            }
            while ( listInFlight.empty() == false && listInFlight.front()._arrivalTick <= tick )
            {
                const OwnerStateInFlight        owner  = listInFlight.front();
                const PredictionReconcileResult result = prediction.reconcile(
                    owner._inputTick, owner._state, client._state,
                    [&client]( const CharacterMoveState& state, const ControlIntent& intent )
                { return client._pMovement->simulate( state, intent, PredictionTestWorld::kTick, CharacterMotionPass::Replay ); },
                    []( const CharacterMoveState& left, const CharacterMoveState& right )
                { return ( left._motion._position - right._motion._position ).getLength() <= 0.01f; } );
                SW_EXPECT_TRUE( result == PredictionReconcileResult::Matched || result == PredictionReconcileResult::Corrected );
                listInFlight.erase( listInFlight.begin() );
            }
        }
        // 같은 입력 틱까지 처리한 서버와 견준다 — 서버가 남은 입력을 마저 돌리면 클라이언트와 같은 자리다.
        for ( uint32 inputTick = static_cast<uint32>( listServerState.size() ); inputTick <= tickCount; ++inputTick )
        {
            server.step( makeIntent( inputTick ) );
        }
        outCorrectionCount = prediction.getCorrectionCount();
        outError           = ( server._state._motion._position - client._state._motion._position ).getLength();
    }
} // namespace

/** @brief [CharacterPredictionTest] 같은 걸음이면 지연 · 손실이 있어도 보정이 없고 서버와 같은 자리다 */
SW_TEST_CASE( CharacterPredictionTest, AgreeingPredictionNeverCorrects )
{
    uint32  correctionCount = 0;
    float32 error           = 0.0f;
    runPrediction( 120, 0, correctionCount, error );
    SW_EXPECT_EQUAL( 0u, correctionCount );
    SW_EXPECT_TRUE( error <= 0.01f );
}

/** @brief [CharacterPredictionTest] 클라이언트가 자기 자리를 옮기면 한 번 되감아 서버의 자리로 돌아온다 — 옮긴 거리는 서버에 닿지 않는다 */
SW_TEST_CASE( CharacterPredictionTest, DivergedPredictionReturnsToTheServer )
{
    uint32  correctionCount = 0;
    float32 error           = 0.0f;
    runPrediction( 120, 60, correctionCount, error );
    SW_EXPECT_TRUE( correctionCount >= 1u );
    SW_EXPECT_TRUE( error <= 0.01f );
}
