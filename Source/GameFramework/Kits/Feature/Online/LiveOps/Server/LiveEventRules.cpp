#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveEventRules.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LiveEventRulesInternal
        {
            static ScheduleDefinition makeSchedule( const LiveEventDefinition& definition )
            {
                ScheduleDefinition schedule;
                schedule._jobID       = definition._eventID;
                schedule._kind        = definition._recurrence == LiveEventRecurrence::Daily ? ScheduleKind::Daily : ScheduleKind::Weekly;
                schedule._minuteOfDay = definition._activeMinuteOfDay;
                schedule._dayOfWeek   = definition._activeDayOfWeek;
                return schedule;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool LiveEventRules::isWindowOpen( const LiveEventDefinition& definition, int64 nowMs, int64& outWindowEndMs )
    {
        if ( nowMs < definition._startMs || nowMs >= definition._endMs )
            return false;
        if ( definition._recurrence == LiveEventRecurrence::Once )
        {
            outWindowEndMs = definition._endMs;
            return true;
        }
        const int64 occurrenceMs = ServiceScheduler::computeLatestOccurrence( LiveEventRulesInternal::makeSchedule( definition ), nowMs );
        if ( occurrenceMs < 0 || nowMs >= occurrenceMs + definition._activeDurationMs )
            return false;
        outWindowEndMs = std::min( occurrenceMs + definition._activeDurationMs, definition._endMs );
        return true;
    }

    bool LiveEventRules::isAudienceMatch( const LiveEventDefinition& definition, AccountID accountID, string_view region, uint32 buildVersion )
    {
        if ( buildVersion < definition._minBuildVersion )
            return false;
        if ( definition._listRegion.empty() == false && std::find( definition._listRegion.begin(), definition._listRegion.end(), region ) == definition._listRegion.end() )
            return false;
        if ( definition._rolloutBasisPoints >= RemoteConfigValue::kFullRolloutPoints )
            return true;
        return RemoteConfig::computeRolloutBucket( definition._eventID, accountID ) < definition._rolloutBasisPoints;
    }

    bool LiveEventRules::isValid( const LiveEventDefinition& definition )
    {
        const bool bIDOk     = LiveOpsLimit::isValidKey( definition._eventID, LiveOpsLimit::kMaxIDSize ) && definition._kind.size() <= static_cast<size_t>( LiveOpsLimit::kMaxKindSize );
        const bool bTimeOk   = definition._endMs > definition._startMs;
        const bool bRepeatOk = definition._recurrence == LiveEventRecurrence::Once ||
                               ( definition._activeDurationMs > 0 && ServiceScheduler::isValidDefinition( LiveEventRulesInternal::makeSchedule( definition ) ) );
        const bool bRolloutOk = 0 <= definition._rolloutBasisPoints && definition._rolloutBasisPoints <= RemoteConfigValue::kFullRolloutPoints;
        const bool bCountOk   = static_cast<int32>( definition._listParameter.size() ) <= LiveOpsLimit::kMaxParameterCount &&
                              static_cast<int32>( definition._listRegion.size() ) <= LiveOpsLimit::kMaxRegionCount;
        if ( bIDOk == false || bTimeOk == false || bRepeatOk == false || bRolloutOk == false || bCountOk == false ||
             definition._recurrence >= LiveEventRecurrence::Count )
            return false;
        for ( const LiveEventParameter& parameter : definition._listParameter )
        {
            if ( LiveOpsLimit::isValidKey( parameter._key, LiveOpsLimit::kMaxParameterKey ) == false ||
                 parameter._value.size() > static_cast<size_t>( LiveOpsLimit::kMaxParameterValue ) )
                return false;
        }
        for ( const string& region : definition._listRegion )
        {
            if ( ServerRecord::isValidName( region ) == false )
                return false;
        }
        return true;
    }
} // namespace sw
