// MovementSnapshotTest — 공중에서 가속하던 캐릭터를 스냅샷으로 옮겨도 같은 의도 열이 같은 궤적을 낸다(수평 속도 · 수직 속도 · 공중 관성이 상태 칸이다). 실제 3D 물리(Jolt), 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

using namespace sw;

namespace
{
    struct TestMovementSnapshotWorld
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        GameObjectManager _manager{};
        GameObject*       _pWalker{ nullptr };

        /** @brief 의도 (앞으로)를 @p frameCount 프레임 동안 냅니다. */
        void drive( uint32 frameCount )
        {
            ControlIntent intent;
            intent._move                           = float2{ 0.0f, 1.0f };
            IntentTrackControllerComponent* pTrack = _manager.createGameObject( hashed_string( "Track" ) )->addComponent<IntentTrackControllerComponent>();
            pTrack->play( *_pWalker->getComponent<PawnComponent>(), vector<ControlIntent>( frameCount, intent ), false );
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kFrame );
            }
        }
    };
} // namespace

/** @brief [MovementSnapshotTest] 가속 중 · 쏘여 떠 있는 캐릭터를 스냅샷에서 되살려 같은 의도를 내면 속도가 원본과 같다 */
SW_TEST_CASE( MovementSnapshotTest, RestoredCharacterContinuesTheSameMotion )
{
    TestMovementSnapshotWorld original;
    original._pWalker                         = original._manager.createGameObject( hashed_string( "Walker" ) );
    CharacterControllerComponent* pController = original._pWalker->addComponent<CharacterControllerComponent>();
    pController->setLocalPosition( float3{ 0.0f, 50.0f, 0.0f } ); // 바닥 없음 — 떠 있다
    original._pWalker->addComponent<PawnComponent>()->setAutoPossess( PawnAutoPossess::None );
    (void)original._pWalker->addComponent<CharacterMovementComponent>();
    original._manager.beginPlay();
    original._manager.update( TestMovementSnapshotWorld::kFrame ); // 첫 물리 프레임이 캐릭터를 짓는다
    pController->launch( float3{ 2.0f, 6.0f, 0.0f } );
    original.drive( 4 ); // 걷는 속도(4 m/s)에 닿기 전 — 가속 중이다
    const CharacterMovementComponent* pMovement = original._pWalker->getComponent<CharacterMovementComponent>();
    SW_REQUIRE_TRUE( 0.5f < pMovement->getHorizontalVelocity()._z && pMovement->getHorizontalVelocity()._z < 3.5f );
    const expected<string> state = ObjectStateSerializer::saveToXMLString( original._pWalker, ObjectSaveOptions{ SerializePurpose::Snapshot } );
    SW_REQUIRE_TRUE( state.has_value() );

    TestMovementSnapshotWorld restored;
    restored._pWalker = restored._manager.createGameObject( hashed_string( "Walker" ) );
    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromXMLString( restored._pWalker, *state, ObjectLoadContext{ SerializePurpose::Snapshot } ) );
    restored._manager.beginPlay();

    original.drive( 3 );
    restored.drive( 3 );
    const CharacterMovementComponent*   pRestoredMovement   = restored._pWalker->getComponent<CharacterMovementComponent>();
    const CharacterControllerComponent* pRestoredController = restored._pWalker->getComponent<CharacterControllerComponent>();
    SW_REQUIRE_TRUE( pRestoredMovement != nullptr && pRestoredController != nullptr );
    SW_EXPECT_NEAR( pMovement->getHorizontalVelocity()._z, pRestoredMovement->getHorizontalVelocity()._z, 1.0e-4f );
    const float3 expectedVelocity = pController->getVelocity();
    const float3 velocity         = pRestoredController->getVelocity();
    SW_EXPECT_NEAR( expectedVelocity._x, velocity._x, 1.0e-3f ); // 발사의 공중 관성
    SW_EXPECT_NEAR( expectedVelocity._y, velocity._y, 1.0e-3f ); // 수직 속도
    SW_EXPECT_NEAR( expectedVelocity._z, velocity._z, 1.0e-3f ); // 가속하던 걸음
}
