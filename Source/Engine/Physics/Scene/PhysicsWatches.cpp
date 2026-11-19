/**
 * @file PhysicsWatches.cpp
 * @brief 물리의 자동화 감시 값입니다 — `Physics.AwakeBodies`(씬의 깨어 있는 동적 바디 수) · `Physics.StreamingHeldBodies`(바닥 셀이 없어 쥔 바디 수) · `Object.Speed`(대상 오브젝트의 속력).
 * @details 시나리오가 "물리가 잠들 때까지" · "멈출 때까지" 를 조건으로 기다리게 한다 — 고정 프레임 대기(N 프레임이면 섰겠지)를 대신한다. 감시 값 레지스트리가 아래층이라
 *          물리가 자기 감시 값을 등록한다(애니메이션 `AnimationWatches` 와 같은 자리). 언리얼 자동화의 "wait for physics sleep" · 유니티 `Rigidbody.IsSleeping` 과 같은 판정이다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

namespace sw
{
    namespace
    {
        struct PhysicsWatchesInternal
        {
            /** @brief 감시 값 `Physics.AwakeBodies` 입니다. 씬의 동적 바디(3D · 2D) 가운데 잠들지 않은 수입니다. */
            [[nodiscard]] static bool readAwakeBodies( SceneIterationKey key, const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 awake = 0;
                pManager->forEachComponentOfType<RigidBodyComponent>( key, [&awake]( const RigidBodyComponent* pBody )
                { awake += pBody->getBodyType() == PhysicsBodyType::Dynamic && pBody->isSleeping() == false ? 1u : 0u; } );
                pManager->forEachComponentOfType<RigidBody2DComponent>( key, [&awake]( const RigidBody2DComponent* pBody )
                { awake += pBody->getBodyType() == PhysicsBodyType::Dynamic && pBody->isSleeping() == false ? 1u : 0u; } );
                outValue = static_cast<float64>( awake );
                return true;
            }

            /** @brief 감시 값 `Physics.StreamingHeldBodies` 입니다. 바닥(스트리밍 셀)이 없어 시뮬레이션에서 뺀 동적 강체 · 캐릭터 컨트롤러 수입니다(`PhysicsComponent::isStreamingHeld`). */
            [[nodiscard]] static bool readStreamingHeldBodies( SceneIterationKey key, const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 held = 0;
                pManager->forEachComponentOfType<RigidBodyComponent>( key, [&held]( const RigidBodyComponent* pBody )
                { held += pBody->isStreamingHeld() ? 1u : 0u; } );
                pManager->forEachComponentOfType<CharacterControllerComponent>( key, [&held]( const CharacterControllerComponent* pCharacter )
                { held += pCharacter->isStreamingHeld() ? 1u : 0u; } );
                outValue = static_cast<float64>( held );
                return true;
            }

            /** @brief 질의 감시 값 `Object.StreamingHeld` 입니다. 대상의 강체 · 캐릭터 컨트롤러 가운데 하나라도 바닥 셀이 없어 쥐였으면 1, 아니면(물리 컴포넌트가 없어도) 0 입니다. */
            [[nodiscard]] static bool readObjectStreamingHeld( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject( outValue );
                if ( pTarget == nullptr )
                    return false;
                bool bHeld = false;
                pTarget->forEachComponentOfType<RigidBodyComponent>( [&bHeld]( const RigidBodyComponent* pBody )
                { bHeld = bHeld || pBody->isStreamingHeld(); } );
                pTarget->forEachComponentOfType<CharacterControllerComponent>( [&bHeld]( const CharacterControllerComponent* pCharacter )
                { bHeld = bHeld || pCharacter->isStreamingHeld(); } );
                outValue.setNumber( bHeld ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 질의 감시 값 `Object.Speed` 입니다. 대상의 강체(3D · 2D) 선속도나 캐릭터 컨트롤러 속도의 크기(m/s)입니다. 둘 다 없으면 값이 없습니다. */
            [[nodiscard]] static bool readObjectSpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject( outValue );
                if ( pTarget == nullptr )
                    return false;
                if ( const RigidBodyComponent* pBody = pTarget->getComponent<RigidBodyComponent>(); pBody != nullptr )
                {
                    outValue.setNumber( static_cast<float64>( pBody->getLinearVelocity().getLength() ) );
                    return true;
                }
                if ( const RigidBody2DComponent* pBody2D = pTarget->getComponent<RigidBody2DComponent>(); pBody2D != nullptr )
                {
                    outValue.setNumber( static_cast<float64>( pBody2D->getLinearVelocity().getLength() ) );
                    return true;
                }
                if ( const CharacterControllerComponent* pCharacter = pTarget->getComponent<CharacterControllerComponent>(); pCharacter != nullptr )
                {
                    outValue.setNumber( static_cast<float64>( pCharacter->getVelocity().getLength() ) );
                    return true;
                }
                if ( const CharacterController2DComponent* pCharacter2D = pTarget->getComponent<CharacterController2DComponent>(); pCharacter2D != nullptr )
                {
                    outValue.setNumber( static_cast<float64>( pCharacter2D->getVelocity().getLength() ) );
                    return true;
                }
                outValue.setText( query.describeMissing( "rigid body or character controller" ) );
                return false;
            }
        };
    } // namespace

    SW_AUTOMATION_SCENE_WATCH( physicsAwakeBodies, "Physics.AwakeBodies", "Dynamic rigid bodies (3D and 2D) of the scene that are not asleep - 0 once everything has come to rest",
                               &PhysicsWatchesInternal::readAwakeBodies );
    SW_AUTOMATION_SCENE_WATCH( physicsStreamingHeldBodies, "Physics.StreamingHeldBodies",
                               "Dynamic rigid bodies and character controllers held out of the simulation because the streaming cell under them is not loaded",
                               &PhysicsWatchesInternal::readStreamingHeldBodies );
    SW_AUTOMATION_QUERY_WATCH( objectStreamingHeld, "Object.StreamingHeld",
                               "1 when the target's rigid body or character controller is held out of the simulation because the streaming cell under it is not loaded and nothing supports it",
                               &PhysicsWatchesInternal::readObjectStreamingHeld, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectSpeed, "Object.Speed", "Speed of the target's rigid body or character controller (m/s)", &PhysicsWatchesInternal::readObjectSpeed, true, false );
} // namespace sw
