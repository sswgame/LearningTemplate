/**
 * @file ScheduleWatchs.cpp
 * @brief NPC 일정의 자동화 감시 값입니다. `Schedule.Activity` · `Schedule.Phase` · `Schedule.DistanceToPlace` · `Schedule.DistanceToTarget` · `Schedule.MinuteOfDay` — 살아 있는 일정 시스템에서 NPC id 로 찾습니다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"

namespace sw
{
    namespace
    {
        /** @brief 살아 있는 일정 시스템에서 NPC id @p npcID 를 찾습니다. 없으면 이유를 적고 false 입니다. */
        [[nodiscard]] bool findNPC( string_view npcID, const ScheduleSystem*& outpSystem, int32& outNPCIndex, AutomationWatchValue& outValue )
        {
            vector<const ScheduleSystem*> listSystem;
            ScheduleSystem::collectLiveSystems( listSystem );
            const hashed_string id( npcID );
            for ( const ScheduleSystem* pSystem : listSystem )
            {
                const int32 npcIndex = pSystem->findNPCIndex( id );
                if ( npcIndex >= 0 )
                {
                    outpSystem  = pSystem;
                    outNPCIndex = npcIndex;
                    return true;
                }
            }
            outValue.setText( listSystem.empty() ? string( "no schedule system is running" ) : "no schedule NPC '" + string( npcID ) + "'" );
            return false;
        }

        /** @brief 감시 값 `Schedule.Activity` 입니다. NPC 가 지금(가는 중이면 가서 할) 블록의 활동 이름이고, 없으면 `none` 입니다. */
        [[nodiscard]] bool readScheduleActivity( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const ScheduleSystem* pSystem  = nullptr;
            int32                 npcIndex = -1;
            if ( findNPC( query._target, pSystem, npcIndex, outValue ) == false )
                return false;
            const ScheduleNPCView view = pSystem->getNPCView( npcIndex );
            outValue.setText( view._activity.empty() ? string_view{ "none" } : string_view{ view._activity.c_str() } );
            return true;
        }

        /** @brief 감시 값 `Schedule.Phase` 입니다. `Traveling` · `Performing` · `Interrupted` 입니다. */
        [[nodiscard]] bool readSchedulePhase( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const ScheduleSystem* pSystem  = nullptr;
            int32                 npcIndex = -1;
            if ( findNPC( query._target, pSystem, npcIndex, outValue ) == false )
                return false;
            switch ( pSystem->getNPCView( npcIndex )._phase )
            {
                case ScheduleNPCPhase::Traveling:
                {
                    outValue.setText( "Traveling" );
                    break;
                }
                case ScheduleNPCPhase::Performing:
                {
                    outValue.setText( "Performing" );
                    break;
                }
                case ScheduleNPCPhase::Interrupted:
                {
                    outValue.setText( "Interrupted" );
                    break;
                }
            }
            return true;
        }

        /** @brief 감시 값 `Schedule.DistanceToPlace` 입니다. NPC 의 일정상 위치에서 카탈로그 장소 `key` 까지의 지면 거리(m)입니다. */
        [[nodiscard]] bool readScheduleDistanceToPlace( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const ScheduleSystem* pSystem  = nullptr;
            int32                 npcIndex = -1;
            if ( findNPC( query._target, pSystem, npcIndex, outValue ) == false )
                return false;
            const SchedulePlaceDef* pPlace = pSystem->getCatalog() != nullptr ? pSystem->getCatalog()->findPlace( hashed_string( query._key ) ) : nullptr;
            if ( pPlace == nullptr )
            {
                outValue.setText( "no schedule place '" + string( query._key ) + "'" );
                return false;
            }
            outValue.setNumber( static_cast<float64>( float3::getHorizontalDistance( pSystem->getNPCView( npcIndex )._location._position, pPlace->_position ) ) );
            return true;
        }

        /** @brief 감시 값 `Schedule.DistanceToTarget` 입니다. NPC 의 일정상 위치에서 지금 블록의 목표 위치까지 지면 거리(m)이며, 닿았으면 0 입니다. */
        [[nodiscard]] bool readScheduleDistanceToTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const ScheduleSystem* pSystem  = nullptr;
            int32                 npcIndex = -1;
            if ( findNPC( query._target, pSystem, npcIndex, outValue ) == false )
                return false;
            const ScheduleNPCView view = pSystem->getNPCView( npcIndex );
            outValue.setNumber( static_cast<float64>( float3::getHorizontalDistance( view._location._position, view._target._position ) ) );
            return true;
        }

        /** @brief 감시 값 `Schedule.MinuteOfDay` 입니다. 첫 일정 시스템의 그날 분(0..1439)입니다. */
        [[nodiscard]] bool readScheduleMinuteOfDay( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            vector<const ScheduleSystem*> listSystem;
            ScheduleSystem::collectLiveSystems( listSystem );
            if ( listSystem.empty() )
                return false;
            outValue = static_cast<float64>( listSystem.front()->getMinute() % kScheduleMinutesPerDay );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( scheduleMinuteOfDay, "Schedule.MinuteOfDay", "Minute of day (0..1439) of the first live schedule system", &readScheduleMinuteOfDay );
    SW_AUTOMATION_QUERY_WATCH( scheduleActivity, "Schedule.Activity", "Activity of the schedule NPC with id target (none between blocks)", &readScheduleActivity, true, false );
    SW_AUTOMATION_QUERY_WATCH( schedulePhase, "Schedule.Phase", "Traveling, Performing or Interrupted for the schedule NPC with id target", &readSchedulePhase, true, false );
    SW_AUTOMATION_QUERY_WATCH( scheduleDistanceToPlace, "Schedule.DistanceToPlace", "Ground distance (m) from the schedule NPC target to the catalog place key",
                               &readScheduleDistanceToPlace, true, true );
    SW_AUTOMATION_QUERY_WATCH( scheduleDistanceToTarget, "Schedule.DistanceToTarget", "Ground distance (m) from the schedule NPC target to its current block's spot (0 once there)",
                               &readScheduleDistanceToTarget, true, false );
} // namespace sw
