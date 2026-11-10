/**
 * @file PhysicsDebugSections.cpp
 * @brief 런타임 물리 접촉 디버거(HUD 섹션 `contacts`)와 캐릭터 이동 디버거(`movement`)입니다. 읽기만 한다.
 * @details 셰이프 와이어프레임(Physics Shapes 시각화)과 물리 HUD 통계는 형상 · 전체 수를 본다. 여기는 대상 하나의 시뮬레이션 상태다 —
 *          접촉 · 트리거의 시작과 끝(고정 용량 기록 `ScenePhysics::collectContactLog`, Jolt · Box2D 같은 의미), 이동 요청 → 내비 희망/회피 → 실제 속도,
 *          접지 · 바닥 법선과 경사 한계, 디딘 바디가 실어 나른 속도, 켰으면(`movement.history on`) 최근 스텝 기록. 언리얼 `p.VisualizeMovement` ·
 *          Chaos 디버거의 접촉 목록과 같은 자리다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"

namespace sw
{
    namespace
    {
        struct PhysicsDebugSectionsInternal
        {
            static string formatVector( const float3& value )
            {
                return "(" + to_string( value._x ) + ", " + to_string( value._y ) + ", " + to_string( value._z ) + ")";
            }

            static string formatObject( const GameObjectManager& manager, uint64 objectID )
            {
                const GameObject* pObject = objectID != 0 ? manager.findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? string( pObject->getName().c_str() ) : "#" + to_string( objectID );
            }

            /** @brief `contacts` — 대상(`debug.target`)이 낀 최근 시작 · 끝, 대상을 정하지 않았으면 전부. */
            static void writeContacts( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    writer.addLine( "scene", "none" );
                    return;
                }
                const GameObject* pTarget = GameplayDebugTarget::findTargetObject( *pManager );
                writer.addLine( "target", pTarget != nullptr ? string( pTarget->getName().c_str() ) : string( "all objects" ) );
                vector<PhysicsContactLogEntry> listEntry;
                pManager->getSubsystem<ScenePhysics>().collectContactLog( listEntry );
                uint32 lineCount = 0;
                for ( auto it = listEntry.rbegin(); it != listEntry.rend(); ++it )
                {
                    const bool bInvolvesTarget = pTarget == nullptr || it->_objectA == pTarget->getObjectID() || it->_objectB == pTarget->getObjectID();
                    if ( bInvolvesTarget == false )
                        continue;
                    if ( ++lineCount > GameplayDebugTarget::kMaxLines )
                        break;
                    string value = string( getEnumName( it->_phase ) ) + " " + formatObject( *pManager, it->_objectA ) + " <-> " + formatObject( *pManager, it->_objectB );
                    value += it->_bOverlap == SW_TRUE ? string( " overlap" ) : " impulse " + to_string( it->_impulse ) + " n " + formatVector( it->_normal );
                    writer.addLine( "step " + to_string( it->_stepIndex ) + ( it->_bIs2D == SW_TRUE ? " 2D" : "" ), value );
                }
                if ( lineCount == 0 )
                    writer.addLine( "contacts", "no begin/end in the recent log" );
            }

            /** @brief 내비 에이전트가 있으면 희망(경로) · 회피 뒤 속도와 다음 코너 · 오프메시 링크. */
            static void writeNavAgent( DebugHUDSectionWriter& writer, const GameObject& object )
            {
                const NavMeshAgentComponent* pAgent = object.getComponent<NavMeshAgentComponent>();
                if ( pAgent == nullptr )
                    return;
                writer.addLine( "nav status", string( getEnumName( pAgent->getMoveStatus() ) ) + ( pAgent->isOnNavMesh() ? "" : " (off navmesh)" ) +
                                                  ( pAgent->isOnOffMeshLink() ? " (on link)" : "" ) );
                writer.addLine( "nav desired", formatVector( pAgent->getDesiredVelocity() ) );
                writer.addLine( "nav avoided", formatVector( pAgent->getVelocity() ) );
                writer.addLine( "nav corner", formatVector( pAgent->getNextCorner() ) );
            }

            static void writeController3D( DebugHUDSectionWriter& writer, const CharacterControllerComponent& controller )
            {
                const PhysicsCharacterState3D& state = controller.getCharacterState();
                writer.addLine( "request", formatVector( controller.getRequestedMoveVelocity() ) );
                if ( controller.getOwner() != nullptr )
                    writeNavAgent( writer, *controller.getOwner() );
                writer.addLine( "actual", formatVector( state._velocity ) );
                const float32 slope = MathUtil::toDegree( MathUtil::acos( MathUtil::clamp( state._groundNormal._y, -1.0f, 1.0f ) ) );
                writer.addLine( "grounded", state._bGrounded ? "yes slope " + to_string( slope ) + " / max " + to_string( MathUtil::toDegree( controller.getMaxSlopeAngle() ) ) : string( "no (air)" ) );
                writer.addLine( "ground normal", formatVector( state._groundNormal ) );
                writer.addLine( "carried", formatVector( state._carriedVelocity ) + " yaw rate " + to_string( state._groundYawRate ) );
                writer.addLine( "capsule", "r " + to_string( controller.getRadius() ) + " half " + to_string( controller.getHalfHeight() ) + " step " + to_string( controller.getStepHeight() ) );
                vector<CharacterControllerComponent::StepRecord> listRecord;
                controller.collectStepHistory( listRecord );
                if ( listRecord.empty() )
                {
                    writer.addLine( "history", CharacterControllerComponent::isStepHistoryEnabled() ? "recording (no step yet)" : "off (movement.history on)" );
                    return;
                }
                int32 age = 0;
                for ( auto it = listRecord.rbegin(); it != listRecord.rend(); ++it, --age )
                {
                    writer.addLine( "step " + to_string( age ), "in " + formatVector( it->_stepVelocity ) + " out " + formatVector( it->_result._velocity ) +
                                                                    ( it->_result._bGrounded ? " grounded" : " air" ) );
                }
            }

            static void writeController2D( DebugHUDSectionWriter& writer, const CharacterController2DComponent& controller )
            {
                const PhysicsCharacterState2D& state = controller.getCharacterState();
                writer.addLine( "request", to_string( controller.getRequestedMoveVelocity() ) );
                writer.addLine( "actual", "(" + to_string( state._velocity._x ) + ", " + to_string( state._velocity._y ) + ")" );
                writer.addLine( "grounded", state._bGrounded ? string( "yes" ) : string( "no (air)" ) );
                writer.addLine( "ground normal", "(" + to_string( state._groundNormal._x ) + ", " + to_string( state._groundNormal._y ) + ")" );
                writer.addLine( "max slope", to_string( MathUtil::toDegree( controller.getMaxSlopeAngle() ) ) );
            }

            /** @brief `movement` — 대상의 3D 컨트롤러, 없으면 2D 컨트롤러(같은 의미의 줄). */
            static void writeMovement( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    writer.addLine( "scene", "none" );
                    return;
                }
                if ( const CharacterControllerComponent* pController = GameplayDebugTarget::findTargetComponent<CharacterControllerComponent>( *pManager ) )
                {
                    writer.addLine( "target", pController->getOwner() != nullptr ? string( pController->getOwner()->getName().c_str() ) + " (3D)" : string( "? (3D)" ) );
                    writeController3D( writer, *pController );
                    return;
                }
                if ( const CharacterController2DComponent* pController = GameplayDebugTarget::findTargetComponent<CharacterController2DComponent>( *pManager ) )
                {
                    writer.addLine( "target", pController->getOwner() != nullptr ? string( pController->getOwner()->getName().c_str() ) + " (2D)" : string( "? (2D)" ) );
                    writeController2D( writer, *pController );
                    return;
                }
                writer.addLine( "target", "none (no character controller in the active scene)" );
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( contacts, "contacts", "Physics contacts", 240, DebugHUDSectionFlag::kNone, &PhysicsDebugSectionsInternal::writeContacts );
    SW_DEBUG_HUD_SECTION( movement, "movement", "Character movement", 250, DebugHUDSectionFlag::kNone, &PhysicsDebugSectionsInternal::writeMovement );
} // namespace sw
