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

#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DebugHUDSectionFormat.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
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
            using LineText = StringBuilder<constant::buffer::kMax256>;

            static void appendVector( LineText& inoutText, const float3& value ) { inoutText.appendFormat( "(%#, %#, %#)", value._x, value._y, value._z ); }

            static void appendObject( LineText& inoutText, const GameObjectManager& manager, uint64 objectID )
            {
                const GameObject* pObject = objectID != 0 ? manager.findGameObjectByID( objectID ) : nullptr;
                if ( pObject != nullptr )
                    inoutText.append( pObject->getName().c_str() );
                else
                    inoutText.appendFormat( "#%#", objectID );
            }

            static void addVectorLine( DebugHUDSectionWriter& writer, string_view label, const float3& value )
            {
                writer.addLineFormat( label, "(%#, %#, %#)", value._x, value._y, value._z );
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
                    LineText value;
                    value.appendFormat( "%# ", getEnumName( it->_phase ) );
                    appendObject( value, *pManager, it->_objectA );
                    value.append( " <-> " );
                    appendObject( value, *pManager, it->_objectB );
                    if ( it->_bOverlap == SW_TRUE )
                        value.append( " overlap" );
                    else
                    {
                        value.appendFormat( " impulse %# n ", it->_impulse );
                        appendVector( value, it->_normal );
                    }
                    fixed_string<constant::buffer::kMax64> label;
                    formatstring( label.data(), label.capacity(), "step %#%#", it->_stepIndex, it->_bIs2D == SW_TRUE ? " 2D" : "" );
                    writer.addLine( label.view(), value.view() );
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
                writer.addLineFormat( "nav status", "%#%#%#", getEnumName( pAgent->getMoveStatus() ), pAgent->isOnNavMesh() ? "" : " (off navmesh)", pAgent->isOnOffMeshLink() ? " (on link)" : "" );
                addVectorLine( writer, "nav desired", pAgent->getDesiredVelocity() );
                addVectorLine( writer, "nav avoided", pAgent->getVelocity() );
                addVectorLine( writer, "nav corner", pAgent->getNextCorner() );
            }

            static void writeController3D( DebugHUDSectionWriter& writer, const CharacterControllerComponent& controller )
            {
                const PhysicsCharacterState3D& state = controller.getCharacterState();
                addVectorLine( writer, "request", controller.getRequestedMoveVelocity() );
                if ( controller.getOwner() != nullptr )
                    writeNavAgent( writer, *controller.getOwner() );
                addVectorLine( writer, "actual", state._velocity );
                const float32 slope = MathUtil::toDegree( MathUtil::acos( MathUtil::clamp( state._groundNormal._y, -1.0f, 1.0f ) ) );
                if ( state._bGrounded )
                    writer.addLineFormat( "grounded", "yes slope %# / max %#", slope, MathUtil::toDegree( controller.getMaxSlopeAngle() ) );
                else
                    writer.addLine( "grounded", "no (air)" );
                addVectorLine( writer, "ground normal", state._groundNormal );
                writer.addLineFormat( "carried", "(%#, %#, %#) yaw rate %#", state._carriedVelocity._x, state._carriedVelocity._y, state._carriedVelocity._z, state._groundYawRate );
                writer.addLineFormat( "capsule", "r %# half %# step %#", controller.getRadius(), controller.getHalfHeight(), controller.getStepHeight() );
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
                    LineText value;
                    value.append( "in " );
                    appendVector( value, it->_stepVelocity );
                    value.append( " out " );
                    appendVector( value, it->_result._velocity );
                    value.append( it->_result._bGrounded ? " grounded" : " air" );
                    fixed_string<constant::buffer::kMax32> label;
                    formatstring( label.data(), label.capacity(), "step %#", age );
                    writer.addLine( label.view(), value.view() );
                }
            }

            static void writeController2D( DebugHUDSectionWriter& writer, const CharacterController2DComponent& controller )
            {
                const PhysicsCharacterState2D& state = controller.getCharacterState();
                writer.addLine( "request", to_string( controller.getRequestedMoveVelocity() ) );
                writer.addLineFormat( "actual", "(%#, %#)", state._velocity._x, state._velocity._y );
                writer.addLine( "grounded", state._bGrounded ? string( "yes" ) : string( "no (air)" ) );
                writer.addLineFormat( "ground normal", "(%#, %#)", state._groundNormal._x, state._groundNormal._y );
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
                if ( const CharacterControllerComponent* pController = GameplayDebugTarget::findTargetOrLowestIDComponent<CharacterControllerComponent>( writer.getSceneIterationKey(), *pManager ) )
                {
                    if ( pController->getOwner() != nullptr )
                        writer.addLineFormat( "target", "%# (3D)", pController->getOwner()->getName().c_str() );
                    else
                        writer.addLine( "target", "? (3D)" );
                    writeController3D( writer, *pController );
                    return;
                }
                if ( const CharacterController2DComponent* pController = GameplayDebugTarget::findTargetOrLowestIDComponent<CharacterController2DComponent>( writer.getSceneIterationKey(), *pManager ) )
                {
                    if ( pController->getOwner() != nullptr )
                        writer.addLineFormat( "target", "%# (2D)", pController->getOwner()->getName().c_str() );
                    else
                        writer.addLine( "target", "? (2D)" );
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
