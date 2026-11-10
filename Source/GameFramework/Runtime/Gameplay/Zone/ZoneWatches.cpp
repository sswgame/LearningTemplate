/**
 * @file ZoneWatches.cpp
 * @brief 지역 효과의 감시 값과 진단 HUD 섹션 `zone` 입니다 — 대상이 든 지역(출처) · 위치 값 · 반응(내린 깊이 · 탈출 수 · 화면 가중치) · 이동 방식 · 물리 필드.
 * @details 값 시나리오(늪 · 모래 · 용암 · 바다 · 유황 · 버프 샘 · 스킬 영역 · 즉사 · 무중력 · 빙판 · 가시덩굴)가 이 값으로 판정한다(골든 없음). 읽기만 하고 상태를 바꾸지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DebugHUDSectionFormat.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Gameplay/Zone/JumpEscapeReactionComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ScreenReactionComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/VisualSinkReactionComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneContactComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ZoneWatchesInternal
        {
            /** @brief 대상 @p target 이 든 지역마다 @p visit( 출처, 위치 값 ) 을 부릅니다 — 볼륨과 대상의 표면 지역. */
            template <typename TVisit>
            static void forEachZoneOf( const GameObjectManager& manager, const GameObject& target, TVisit&& visit )
            {
                const GameObjectHandle handle = target.getHandle();
                manager.forEachComponentOfType<ZoneVolumeComponent>( [&]( const ZoneVolumeComponent* pVolume )
                {
                    const ZoneTracker::OccupantRecord* pOccupancy = pVolume->getTracker().findOccupancy( handle );
                    if ( pOccupancy != nullptr && pOccupancy->_bEntered == SW_TRUE )
                        visit( pVolume->getSourceName(), pOccupancy->_sample );
                } );
                const ZoneContactComponent* pContact = target.getComponent<ZoneContactComponent>();
                if ( pContact == nullptr )
                    return;
                for ( const unique_ptr<ZoneContactComponent::SurfaceZone>& pZone : pContact->getSurfaceZones() )
                {
                    const ZoneTracker::OccupantRecord* pOccupancy = pZone->_tracker.findOccupancy( handle );
                    if ( pOccupancy != nullptr && pOccupancy->_bEntered == SW_TRUE )
                        visit( hashed_string( string( "surface:" ) + pZone->_kind.c_str() ), pOccupancy->_sample );
                }
            }

            /** @brief 위치 값 이름의 값입니다. 모르는 이름이면 false 입니다. */
            static bool readSampleValue( const ZoneSample& sample, string_view name, float32& outValue )
            {
                if ( name == "zoneDepth01" )
                    outValue = sample._zoneDepth01;
                else if ( name == "distanceToCenter" )
                    outValue = sample._distanceToCenter;
                else if ( name == "immersion" )
                    outValue = sample._immersion;
                else if ( name == "timeInside" )
                    outValue = sample._timeInside;
                else if ( name == "impactSpeed" )
                    outValue = sample._impactSpeed;
                else
                    return false;
                return true;
            }

            /** @brief 감시 값 `Zone.Value` 입니다. 대상이 든 지역들의 위치 값 중 가장 큰 것이고, 어느 지역에도 없으면 0 입니다. */
            [[nodiscard]] static bool readValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject();
                if ( pTarget == nullptr || query._pManager == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                float32 probe = 0.0f;
                if ( readSampleValue( ZoneSample{}, query._key, probe ) == false )
                {
                    outValue.setText( "key must be zoneDepth01, distanceToCenter, immersion, timeInside or impactSpeed" );
                    return false;
                }
                float32 best = 0.0f;
                forEachZoneOf( *query._pManager, *pTarget, [&]( const hashed_string& /*source*/, const ZoneSample& sample )
                {
                    float32 value = 0.0f;
                    (void)readSampleValue( sample, query._key, value ); // 이름은 위에서 봤다
                    best = MathUtil::max( best, value );
                } );
                outValue.setNumber( static_cast<float64>( best ) );
                return true;
            }

            /** @brief 감시 값 `Zone.Sources` 입니다. 대상이 든 지역의 출처를 이름 순으로 `,` 로 이은 글이고, 없으면 `None` 입니다. */
            [[nodiscard]] static bool readSources( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject();
                if ( pTarget == nullptr || query._pManager == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                vector<string> listName;
                forEachZoneOf( *query._pManager, *pTarget, [&listName]( const hashed_string& source, const ZoneSample& /*sample*/ )
                { listName.push_back( source.c_str() ); } );
                std::sort( listName.begin(), listName.end() );
                string joined;
                for ( const string& name : listName )
                {
                    if ( joined.empty() == false )
                        joined += ',';
                    joined += name;
                }
                outValue.setText( joined.empty() ? string( "None" ) : joined );
                return true;
            }

            /** @brief 대상 볼륨입니다. 없거나 프로필이 돌지 않으면 까닭을 적고 nullptr 입니다. */
            static const ZoneVolumeComponent* findVolume( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*          pTarget = query.findTargetObject();
                const ZoneVolumeComponent* pVolume = pTarget != nullptr ? pTarget->getComponent<ZoneVolumeComponent>() : nullptr;
                if ( pVolume == nullptr || pVolume->isReady() == false )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : ( pVolume == nullptr ? query.describeMissing( "ZoneVolumeComponent" ) : string( "the zone volume's profile did not load" ) ) );
                    return nullptr;
                }
                return pVolume;
            }

            /** @brief 감시 값 `Zone.Modules` 입니다. 대상 볼륨이 도는 모듈 이름(프리셋 · 변형 · 배치 덮어쓰기를 합친 결과)을 차례대로 `,` 로 이은 글입니다. */
            [[nodiscard]] static bool readModules( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const ZoneVolumeComponent* pVolume = findVolume( query, outValue );
                if ( pVolume == nullptr )
                    return false;
                string joined;
                for ( const ZoneCompiledModule& module : pVolume->getCompiledProfile().getModules() )
                {
                    joined += joined.empty() ? string( module._name.c_str() ) : "," + string( module._name.c_str() );
                }
                outValue.setText( joined );
                return true;
            }

            /** @brief 감시 값 `Zone.Param` 입니다. 대상 볼륨이 도는 모듈 매개변수(key `<모듈>.<매개변수>`)의 글(합친 결과 — 배치가 바꾼 값이 보인다)입니다. */
            [[nodiscard]] static bool readParam( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const ZoneVolumeComponent* pVolume = findVolume( query, outValue );
                const size_t               dot     = query._key.find( '.' );
                if ( pVolume == nullptr )
                    return false;
                if ( dot == string_view::npos )
                {
                    outValue.setText( "key must be <module>.<parameter>" );
                    return false;
                }
                const hashed_string moduleName{ query._key.substr( 0, dot ) };
                const hashed_string paramName{ query._key.substr( dot + 1 ) };
                for ( const ZoneCompiledModule& module : pVolume->getCompiledProfile().getModules() )
                {
                    const ZoneCompiledParam* pParam = module._name == moduleName ? module.findParam( paramName ) : nullptr;
                    if ( pParam != nullptr )
                    {
                        outValue.setText( pParam->_text );
                        return true;
                    }
                }
                outValue.setText( "no such module parameter in the zone" );
                return false;
            }

            /** @brief 감시 값 `Zone.SinkVisual` 입니다. 대상의 외형을 내린 깊이(m)입니다. */
            [[nodiscard]] static bool readSinkVisual( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*                  pTarget   = query.findTargetObject();
                const VisualSinkReactionComponent* pReaction = pTarget != nullptr ? pTarget->getComponent<VisualSinkReactionComponent>() : nullptr;
                if ( pReaction == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "VisualSinkReactionComponent" ) );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pReaction->getAppliedDepth() ) );
                return true;
            }

            /** @brief 감시 값 `Zone.ScreenWeight` 입니다. 대상의 화면 반응 가중치(0..1)입니다. */
            [[nodiscard]] static bool readScreenWeight( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*              pTarget   = query.findTargetObject();
                const ScreenReactionComponent* pReaction = pTarget != nullptr ? pTarget->getComponent<ScreenReactionComponent>() : nullptr;
                if ( pReaction == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "ScreenReactionComponent" ) );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pReaction->getWeight() ) );
                return true;
            }

            /** @brief 감시 값 `Zone.EscapeCount` 입니다. 대상이 뛰어서 스택을 줄인 수입니다. */
            [[nodiscard]] static bool readEscapeCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*                  pTarget   = query.findTargetObject();
                const JumpEscapeReactionComponent* pReaction = pTarget != nullptr ? pTarget->getComponent<JumpEscapeReactionComponent>() : nullptr;
                if ( pReaction == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "JumpEscapeReactionComponent" ) );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pReaction->getEscapeCount() ) );
                return true;
            }

            /** @brief 감시 값 `Character.MovementMode` 입니다. 대상 캐릭터가 필드로 고른 이동 방식(`Walking` · `Falling` · `Swimming` · `Floating`)입니다. */
            [[nodiscard]] static bool readMovementMode( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*                   pTarget     = query.findTargetObject();
                const CharacterControllerComponent* pController = pTarget != nullptr ? pTarget->getComponent<CharacterControllerComponent>() : nullptr;
                if ( pController == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "CharacterControllerComponent" ) );
                    return false;
                }
                outValue.setText( getEnumName( pController->getMovementMode() ) );
                return true;
            }

            /** @brief 감시 값 `Physics.Field` 입니다. 대상(캐릭터 · 강체)이 지난 물리 프레임에 받은 필드 칸(key: gravityScale · drag · fluidDensity · friction · immersion · flowSpeed)입니다. */
            [[nodiscard]] static bool readField( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*                   pTarget     = query.findTargetObject();
                const CharacterControllerComponent* pController = pTarget != nullptr ? pTarget->getComponent<CharacterControllerComponent>() : nullptr;
                const RigidBodyComponent*           pBody       = pTarget != nullptr ? pTarget->getComponent<RigidBodyComponent>() : nullptr;
                if ( pController == nullptr && pBody == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "CharacterControllerComponent or RigidBodyComponent" ) );
                    return false;
                }
                const PhysicsFieldSample& field = pController != nullptr ? pController->getPhysicsField() : pBody->getPhysicsField();
                if ( query._key == "gravityScale" )
                    outValue.setNumber( field._gravityScale );
                else if ( query._key == "drag" )
                    outValue.setNumber( field._drag );
                else if ( query._key == "fluidDensity" )
                    outValue.setNumber( field._fluidDensity );
                else if ( query._key == "friction" )
                    outValue.setNumber( field._friction );
                else if ( query._key == "immersion" )
                    outValue.setNumber( field._immersion );
                else if ( query._key == "flowSpeed" )
                    outValue.setNumber( field._flowVelocity.getLength() );
                else
                {
                    outValue.setText( "key must be gravityScale, drag, fluidDensity, friction, immersion or flowSpeed" );
                    return false;
                }
                return true;
            }

            /** @brief HUD 섹션 `zone` — 디버그 대상이 든 지역의 출처 · 위치 값과 최근 진입 · 이탈입니다. */
            static void writeZone( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                // 대상은 지역을 받는 개체다 — 캐릭터(컨트롤러)가 먼저, 없으면 표면 입구를 가진 물체.
                const CharacterControllerComponent* pController = pManager != nullptr ? GameplayDebugTarget::findTargetComponent<CharacterControllerComponent>( *pManager ) : nullptr;
                const ZoneContactComponent*         pContact    = pManager != nullptr && pController == nullptr ? GameplayDebugTarget::findTargetComponent<ZoneContactComponent>( *pManager ) : nullptr;
                const GameObject* const             pTarget     = pController != nullptr ? pController->getOwner() : ( pContact != nullptr ? pContact->getOwner() : nullptr );
                if ( pTarget == nullptr )
                {
                    writer.addLine( "target", "none (debug.target <object>)" );
                    return;
                }
                uint32 zoneCount = 0;
                forEachZoneOf( *pManager, *pTarget, [&]( const hashed_string& source, const ZoneSample& sample )
                {
                    writer.addLineFormat( source.c_str(), "depth %# dist %# immersion %# time %# impact %#", sample._zoneDepth01, sample._distanceToCenter, sample._immersion,
                                          sample._timeInside, sample._impactSpeed );
                    ++zoneCount;
                } );
                if ( zoneCount == 0 )
                    writer.addLine( pTarget->getName().c_str(), "in no zone" );
                if ( pController != nullptr )
                {
                    const PhysicsFieldSample& field = pController->getPhysicsField();
                    writer.addLineFormat( "movement", "%# gravity %# drag %# fluid %# friction %#", getEnumName( pController->getMovementMode() ), field._gravityScale, field._drag,
                                          field._fluidDensity, field._friction );
                }
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( zoneValue, "Zone.Value", "Largest zone value (key: zoneDepth01, distanceToCenter, immersion, timeInside, impactSpeed) over the zones the target is in; 0 outside every zone",
                               &ZoneWatchesInternal::readValue, true, true );
    SW_AUTOMATION_QUERY_WATCH( zoneSources, "Zone.Sources", "Zones the target is in (volume:<name>, surface:<kind>) sorted and joined by ','; None outside every zone", &ZoneWatchesInternal::readSources,
                               true, false );
    SW_AUTOMATION_QUERY_WATCH( zoneModules, "Zone.Modules", "Modules the target zone volume runs (preset, variant and placement overrides merged) joined by ','", &ZoneWatchesInternal::readModules,
                               true, false );
    SW_AUTOMATION_QUERY_WATCH( zoneParam, "Zone.Param", "Text of a module parameter the target zone volume runs (key = <module>.<parameter>)", &ZoneWatchesInternal::readParam, true, true );
    SW_AUTOMATION_QUERY_WATCH( zoneSinkVisual, "Zone.SinkVisual", "Metres the target's look is lowered by its VisualSinkReactionComponent", &ZoneWatchesInternal::readSinkVisual, true, false );
    SW_AUTOMATION_QUERY_WATCH( zoneScreenWeight, "Zone.ScreenWeight", "Weight (0..1) of the post-process look the target's ScreenReactionComponent blends into cameras following it",
                               &ZoneWatchesInternal::readScreenWeight, true, false );
    SW_AUTOMATION_QUERY_WATCH( zoneEscapeCount, "Zone.EscapeCount", "Jumps that removed stacks through the target's JumpEscapeReactionComponent", &ZoneWatchesInternal::readEscapeCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( characterMovementMode, "Character.MovementMode", "Movement mode the target's character controller picked from its physics field (Walking, Falling, Swimming, Floating)",
                               &ZoneWatchesInternal::readMovementMode, true, false );
    SW_AUTOMATION_QUERY_WATCH( physicsField, "Physics.Field", "Physics field the target received last physics frame (key: gravityScale, drag, fluidDensity, friction, immersion, flowSpeed)",
                               &ZoneWatchesInternal::readField, true, true );
    SW_DEBUG_HUD_SECTION( zone, "zone", "Zones the debug target is in, zone values and movement field", 285, DebugHUDSectionFlag::kNone, &ZoneWatchesInternal::writeZone );
} // namespace sw
