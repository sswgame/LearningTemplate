/**
 * @file PhysicsWatches.cpp
 * @brief 물리의 자동화 감시 값입니다 — `Physics.AwakeBodies`(씬의 깨어 있는 동적 바디 수) · `Physics.StreamingHeldBodies`(바닥 셀이 없어 쥔 바디 수) · `Object.Speed`(대상 오브젝트의 속력) · `Physics.OverlapCount`(대상이 낀 겹침 쌍 수) · `Physics.TraceDistance`(트레이스 채널 광선 거리).
 * @details 시나리오가 "물리가 잠들 때까지" · "멈출 때까지" 를 조건으로 기다리게 한다 — 고정 프레임 대기(N 프레임이면 섰겠지)를 대신한다. 감시 값 레지스트리가 아래층이라
 *          물리가 자기 감시 값을 등록한다(애니메이션 `AnimationWatches` 와 같은 자리). 언리얼 자동화의 "wait for physics sleep" · 유니티 `Rigidbody.IsSleeping` 과 같은 판정이다.
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Environment/Wind/WindComponent.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Physics/Cloth/ClothComponent.h"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/JointComponent.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Physics/Scene/PhysicsPoseStore.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Visual/Component/MeshComponent.h"

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

            /**
             * @brief 질의 감시 값 `Physics.OverlapCount` 입니다. 대상 오브젝트가 낀 겹침(트리거 · 센서) 오브젝트 쌍 수입니다 — 트리거 계약의 진입 · 이탈과 같은 장부(`PhysicsOverlapTracker`).
             * @details 트리거에 대면 안에 든 대상 수, 대상에 대면 들어 있는 트리거 수다(트리거 여부가 다른 쌍은 따로 센다). 시나리오가 "안에서 생기면 1, 지우면 0" 을 기다린다.
             */
            [[nodiscard]] static bool readOverlapCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject( outValue );
                if ( pTarget == nullptr || pTarget->getManager() == nullptr )
                    return false;
                const ScenePhysics& physics = pTarget->getManager()->getSubsystem<ScenePhysics>();
                outValue.setNumber( static_cast<float64>( physics.getOverlapTracker().getOverlapCount( pTarget->getObjectID() ) ) );
                return true;
            }

            /**
             * @brief 질의 감시 값 `Physics.TraceDistance` 입니다. 대상의 자리에서 대상의 로컬 +X 쪽으로 트레이스 채널(키) 광선을 쏴 처음 맞은 거리(m)입니다. 안 맞으면 0 입니다.
             * @details 대상 자신의 바디는 보지 않는다. 시나리오가 "카메라 채널은 캐릭터를 무시하고 벽에 맞는다" 를 본다(`ScenePhysics::makeTraceFilter`).
             */
            [[nodiscard]] static bool readTraceDistance( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                constexpr float32 kMaxDistance = 100.0f;
                const GameObject* pTarget      = query.findTargetObject( outValue );
                if ( pTarget == nullptr || pTarget->getManager() == nullptr || pTarget->getPrimarySceneComponent() == nullptr )
                    return false;
                const ScenePhysics&          physics = pTarget->getManager()->getSubsystem<ScenePhysics>();
                const IPhysicsScene3D* const pScene  = physics.findScene3D();
                optional<PhysicsQueryFilter> filter  = physics.makeTraceFilter( hashed_string( query._key ) );
                if ( pScene == nullptr || filter.has_value() == false )
                {
                    outValue.setText( pScene == nullptr ? string( "the scene has no 3D physics" ) : string( "unknown trace channel" ) );
                    return false;
                }
                filter->_ignoreUserData                     = pTarget->getObjectID();
                const SceneComponent* const pSceneComponent = pTarget->getPrimarySceneComponent();
                const float4x4              world           = pSceneComponent->getWorldMatrix();
                const float3                origin          = pSceneComponent->getWorldPosition();
                const float3                ahead           = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, world ) - float3::transform( float3{}, world );
                PhysicsCastHit3D            hit;
                const bool                  bHit = ahead.getLength() > 0.0f && pScene->raycast( origin, ahead * ( 1.0f / ahead.getLength() ), kMaxDistance, *filter, hit );
                outValue.setNumber( bHit ? static_cast<float64>( hit._distance ) : 0.0 );
                return true;
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
    SW_AUTOMATION_QUERY_WATCH( physicsOverlapCount, "Physics.OverlapCount", "Overlap (trigger or sensor) object pairs the target is in - inside a trigger counts 1, the trigger counts its occupants",
                               &PhysicsWatchesInternal::readOverlapCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( physicsTraceDistance, "Physics.TraceDistance", "Distance (m) a ray of the trace channel (key) cast from the target along its local +X travels before it hits; 0 when it hits nothing",
                               &PhysicsWatchesInternal::readTraceDistance, true, true );
} // namespace sw

// Engine/Physics/Component/JointComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( jointPosition, "Joint.Position", "Hinge angle (rad), slider distance or distance length of the first joint", JointComponent, &JointComponent::getJointPosition );
} // namespace sw

// Engine/Physics/Cloth/ClothComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Cloth.HorizontalOffset` 입니다. 대상 천의 아랫줄 가운데가 윗줄 가운데에서 수평으로 벗어난 거리(m)입니다 — 바람에 날린 정도. */
        [[nodiscard]] bool readClothHorizontalOffset( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const ClothComponent* pCloth = query.findTargetComponent<ClothComponent>( "Cloth", outValue );
            if ( pCloth == nullptr )
                return false;
            const vector<float3>& listVertex = pCloth->getVertices();
            const size_t          columns    = pCloth->getColumnCount();
            if ( columns == 0 || listVertex.size() < columns * 2 )
            {
                outValue.setText( "cloth not created" );
                return false;
            }
            float3 offset{};
            for ( size_t column = 0; column < columns; ++column )
                offset += listVertex[listVertex.size() - columns + column] - listVertex[column];
            outValue.setNumber( static_cast<float64>( float2{ offset._x, offset._z }.getLength() / static_cast<float32>( columns ) ) );
            return true;
        }

        /** @brief 감시 값 `Cloth.HighestY` · `Cloth.LowestY` 입니다 — 대상 천의 정점 월드 높이(m) 가운데 가장 높은 · 낮은 값. 천이 없으면(아직 만들지 않았거나 실패) 값이 없습니다. */
        template <bool bHighest>
        [[nodiscard]] bool readClothHeight( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const ClothComponent* pCloth = query.findTargetComponent<ClothComponent>( "cloth", outValue );
            if ( pCloth == nullptr || pCloth->getVertices().empty() )
                return false;
            float32 height = pCloth->getVertices().front()._y;
            for ( const float3& vertex : pCloth->getVertices() )
            {
                height = bHighest ? MathUtil::max( height, vertex._y ) : MathUtil::min( height, vertex._y );
            }
            outValue.setNumber( static_cast<float64>( height ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( clothHorizontalOffset, "Cloth.HorizontalOffset", "Horizontal distance (m) from the top edge centre to the bottom edge centre of the target's cloth", &readClothHorizontalOffset, true, false );
    SW_AUTOMATION_QUERY_WATCH( clothHighestY, "Cloth.HighestY", "World height (m) of the target cloth's highest vertex", &readClothHeight<true>, true, false );
    SW_AUTOMATION_QUERY_WATCH( clothLowestY, "Cloth.LowestY", "World height (m) of the target cloth's lowest vertex", &readClothHeight<false>, true, false );
    // 천 LOD 감시 값(시나리오 `game/playground/automation/clothlod`) — 씬의 첫 천을 읽는다.
    SW_AUTOMATION_GETTER_WATCH( clothIterationCount, "Cloth.IterationCount", "Solver iterations given to the first cloth's soft body (0: no soft body, e.g. suspended by LOD)", ClothComponent, &ClothComponent::getAppliedIterationCount );
    SW_AUTOMATION_GETTER_WATCH( clothLODSuspended, "Cloth.LODSuspended", "1 while the first cloth is suspended by its LOD (_maxLOD) and shows its rest grid", ClothComponent, &ClothComponent::isLODSuspended );
    SW_AUTOMATION_GETTER_WATCH( clothLODGrade, "Cloth.LODGrade", "Animation LOD grade the first cloth reads from its ancestor skeletal mesh", ClothComponent, &ClothComponent::readLODGrade );
} // namespace sw

// Engine/Physics/Component/CharacterControllerComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct CharacterControllerHistoryWatchesInternal
        {
            /** @brief `movement.history on|off` — 캐릭터 이동 디버거(HUD 섹션 `movement`)의 스텝 기록. */
            static bool runHistory( const vector<string>& listArgument, string& outReply )
            {
                bool bEnabled = false;
                if ( listArgument.size() != 1 || StringUtil::tryParseBool( listArgument[0], bEnabled ) == false )
                    return false;
                CharacterControllerComponent::setStepHistoryEnabled( bEnabled );
                outReply = bEnabled ? "movement.history on" : "movement.history off";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( MovementHistory, "movement.history", "movement.history on|off",
                    "Record the last character controller steps (request, result) for the movement debugger (hud.print movement)", &CharacterControllerHistoryWatchesInternal::runHistory );
} // namespace sw
