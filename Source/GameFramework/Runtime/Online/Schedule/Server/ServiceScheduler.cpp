#include "pch.h"

#include "GameFramework/Runtime/Online/Schedule/Server/ServiceScheduler.h"

#include "Core/Foundation/Constants.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"

namespace sw
{
    namespace
    {
        struct ServiceSchedulerInternal
        {
            static constexpr int64  kMinuteMs       = constant::time::kSecondsPerMinute * constant::time::kMillisecondsPerSecond;
            static constexpr int64  kDayMs          = 24 * 60 * kMinuteMs;
            static constexpr int64  kDaysPerWeek    = 7;
            static constexpr int64  kEpochWeekday   = 3; ///< 1970-01-01 은 목요일(ISO 월요일 = 0)
            static constexpr int64  kRetryMs        = 5000;
            static constexpr int32  kMaxJobIDSize   = 128;
            static constexpr uint64 kVersion        = 1;
            static constexpr uint32 kStateRunning   = 0;
            static constexpr uint32 kStateSucceeded = 1;
            static constexpr uint32 kStateFailed    = 2;
            static constexpr int32  kStateBitCount  = 2;

            /** @brief 차지 레코드입니다. */
            struct RunRecord
            {
                uint64 _serverID{ 0 };
                int64  _startMs{ 0 };
                int64  _endMs{ 0 };
                uint32 _state{ kStateRunning };
            };

            static string makeKey( string_view jobID, int64 occurrenceMs )
            {
                string key{ jobID };
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, static_cast<uint64>( occurrenceMs ) );
                return key;
            }

            static vector<uint8> encode( const RunRecord& record )
            {
                BitWriter writer;
                writer.writeVarUint( kVersion );
                writer.writeVarUint( record._serverID );
                writer.writeVarInt( record._startMs );
                writer.writeVarInt( record._endMs );
                writer.writeBits( record._state, kStateBitCount );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decode( const vector<uint8>& bytes, RunRecord& outRecord )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readVarUint() != kVersion )
                    return false;
                outRecord._serverID = reader.readVarUint();
                outRecord._startMs  = reader.readVarInt();
                outRecord._endMs    = reader.readVarInt();
                outRecord._state    = reader.readBits( kStateBitCount );
                return reader.hasOverflowed() == false;
            }

            static int64 floorDivide( int64 value, int64 divisor )
            {
                const int64 quotient = value / divisor;
                return ( value % divisor != 0 && ( value < 0 ) != ( divisor < 0 ) ) ? quotient - 1 : quotient;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ServiceScheduler::ClaimReport ServiceScheduler::claimOccurrence( IServiceStoreConnection& connection, const string& jobID, int64 occurrenceMs, uint64 serverID, int64 nowMs,
                                                                     int64 leaseMs )
    {
        ClaimReport attempt;
        attempt._retryAtMs           = nowMs + ServiceSchedulerInternal::kRetryMs;
        const string             key = ServiceSchedulerInternal::makeKey( jobID, occurrenceMs );
        ServiceRecord            record;
        const ServiceStoreResult readResult = connection.readRecord( getTable(), key, record );
        if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
            return attempt;
        uint64 expectedVersion = ServiceRecord::kAbsentVersion;
        if ( readResult == ServiceStoreResult::Ok )
        {
            ServiceSchedulerInternal::RunRecord existing;
            if ( ServiceSchedulerInternal::decode( record._bytes, existing ) == false )
                return attempt; // 깨진 레코드 — 다시 보지 않으면 영영 안 돈다. 운영이 고칠 때까지 다시 해 본다
            if ( existing._state != ServiceSchedulerInternal::kStateRunning )
            {
                attempt._result = ClaimResult::Finished;
                return attempt;
            }
            const int64 leaseEndMs = existing._startMs + leaseMs;
            if ( nowMs < leaseEndMs )
            {
                attempt._result    = ClaimResult::Busy;
                attempt._retryAtMs = leaseEndMs;
                return attempt;
            }
            expectedVersion = record._version; // 임대가 지났다 — 이어받는다
        }
        ServiceSchedulerInternal::RunRecord claim;
        claim._serverID = serverID;
        claim._startMs  = nowMs;
        ServiceTransaction transaction;
        transaction.put( getTable(), key, ServiceSchedulerInternal::encode( claim ), expectedVersion );
        ServiceCommitInfo        info;
        const ServiceStoreResult commitResult = connection.commit( transaction, &info );
        if ( commitResult == ServiceStoreResult::Ok )
        {
            attempt._result   = ClaimResult::Claimed;
            attempt._runToken = info._commitVersion;
            return attempt;
        }
        if ( commitResult == ServiceStoreResult::Conflict )
        {
            attempt._result    = ClaimResult::Busy; // 같은 순간 다른 서버가 차지했다
            attempt._retryAtMs = nowMs + leaseMs;
        }
        return attempt;
    }

    ServiceStoreResult ServiceScheduler::writeFinish( IServiceStoreConnection& connection, const ScheduledRun& run, uint64 serverID, bool bSucceeded, int64 nowMs )
    {
        ServiceSchedulerInternal::RunRecord finished;
        finished._serverID = serverID;
        finished._startMs  = nowMs;
        finished._endMs    = nowMs;
        finished._state    = bSucceeded ? ServiceSchedulerInternal::kStateSucceeded : ServiceSchedulerInternal::kStateFailed;
        ServiceTransaction transaction;
        transaction.put( getTable(), ServiceSchedulerInternal::makeKey( run._jobID, run._occurrenceMs.count() ), ServiceSchedulerInternal::encode( finished ), run._runToken );
        return connection.commit( transaction );
    }
} // namespace sw

namespace sw
{
    ServiceScheduler::ServiceScheduler()
        : _listJob{}
        , _serverID{ 0 }
        , _lastTickMs{ 0 }
        , _store{}
    {
    }

    ServiceScheduler::~ServiceScheduler()
    {
        shutdown();
    }

    void ServiceScheduler::initialize( IServiceStore* pStore, uint64 serverID )
    {
        _store.bindStore( pStore );
        _serverID = serverID;
        _listJob.clear();
    }

    void ServiceScheduler::shutdown()
    {
        _listJob.clear();
        _store.bindStore( nullptr );
    }

    bool ServiceScheduler::registerJob( const ScheduleDef& definition, const ScheduledJobFunction& function )
    {
        if ( function.isBound() == false || isValidDefinition( definition ) == false )
        {
            SW_LOG_WARNING_ID( "ServiceScheduler.IgnoredScheduledJobInvalid", "Ignored scheduled job '%#' — invalid schedule or no handler", definition._jobID );
            return false;
        }
        Job& job        = _listJob.emplace_back();
        job._definition = definition;
        job._function   = function;
        return true;
    }

    void ServiceScheduler::update( UnixMilliseconds nowMs )
    {
        if ( _store.hasStore() == false )
            return;
        _lastTickMs = nowMs;
        for ( size_t jobIndex = 0; jobIndex < _listJob.size(); ++jobIndex )
        {
            Job& job = _listJob[jobIndex];
            if ( job._bClaimPending == SW_TRUE )
                continue;
            const int64 occurrenceMs = computeLatestOccurrence( job._definition, nowMs.count() );
            if ( occurrenceMs < 0 || occurrenceMs <= job._handledOccurrenceMs )
                continue;
            const bool bWaitingRetry = job._retryOccurrenceMs == occurrenceMs && nowMs.count() < job._retryAtMs;
            if ( bWaitingRetry )
                continue;
            job._bClaimPending = SW_TRUE;
            _store.submit( [jobID = job._definition._jobID, occurrenceMs, serverID = _serverID, now = nowMs.count(), leaseMs = job._definition._leaseMs]( IServiceStoreConnection& connection )
            { return claimOccurrence( connection, jobID, occurrenceMs, serverID, now, leaseMs ); },
                           [this, jobIndex, occurrenceMs]( const ClaimReport& attempt )
            { onClaimCompleted( jobIndex, occurrenceMs, attempt._result, attempt._runToken, attempt._retryAtMs ); } );
        }
    }

    void ServiceScheduler::completeRun( const ScheduledRun& run, bool bSucceeded )
    {
        if ( _store.hasStore() == false )
            return;
        _store.submit( [run, serverID = _serverID, bSucceeded, nowMs = _lastTickMs.count()]( IServiceStoreConnection& connection )
        { return writeFinish( connection, run, serverID, bSucceeded, nowMs ); },
                       [jobID = run._jobID]( ServiceStoreResult result, ServiceStoreCompletion completion )
        {
            if ( result != ServiceStoreResult::Ok && completion.isCancelled() == false ) // 종료 때문에 취소된 일은 경고하지 않는다
                SW_LOG_WARNING_ID( "ServiceScheduler.CouldNotRecordEnd", "Could not record the end of scheduled run '%#' (another server took it over or the store is unavailable)", jobID );
        } );
    }

    int64 ServiceScheduler::computeLatestOccurrence( const ScheduleDef& definition, int64 nowMs )
    {
        if ( nowMs < 0 )
            return constant::invalid_index::kInt64;
        const int64 minuteOffsetMs = static_cast<int64>( definition._minuteOfDay ) * ServiceSchedulerInternal::kMinuteMs;
        const int64 dayIndex       = ServiceSchedulerInternal::floorDivide( nowMs, ServiceSchedulerInternal::kDayMs );
        int64       occurrenceMs   = constant::invalid_index::kInt64;
        switch ( definition._kind )
        {
            case ScheduleKind::Daily:
            {
                occurrenceMs = dayIndex * ServiceSchedulerInternal::kDayMs + minuteOffsetMs;
                if ( occurrenceMs > nowMs )
                    occurrenceMs -= ServiceSchedulerInternal::kDayMs;
                break;
            }
            case ScheduleKind::Weekly:
            {
                const int64 weekday   = ( dayIndex + ServiceSchedulerInternal::kEpochWeekday ) % ServiceSchedulerInternal::kDaysPerWeek;
                const int64 daysSince = ( weekday - definition._dayOfWeek + ServiceSchedulerInternal::kDaysPerWeek ) % ServiceSchedulerInternal::kDaysPerWeek;
                occurrenceMs          = ( dayIndex - daysSince ) * ServiceSchedulerInternal::kDayMs + minuteOffsetMs;
                if ( occurrenceMs > nowMs )
                    occurrenceMs -= ServiceSchedulerInternal::kDaysPerWeek * ServiceSchedulerInternal::kDayMs;
                break;
            }
            case ScheduleKind::Window:
            {
                const bool bInside = definition._windowStartMs <= nowMs && nowMs < definition._windowEndMs;
                occurrenceMs       = bInside ? definition._windowStartMs : constant::invalid_index::kInt64;
                break;
            }
            case ScheduleKind::Interval:
            {
                occurrenceMs = ServiceSchedulerInternal::floorDivide( nowMs, definition._intervalMs ) * definition._intervalMs;
                break;
            }
        }
        return occurrenceMs >= 0 ? occurrenceMs : constant::invalid_index::kInt32;
    }

    bool ServiceScheduler::isValidDefinition( const ScheduleDef& definition )
    {
        const bool bInvalidJobDefinition = definition._jobID.empty() || definition._jobID.size() > static_cast<size_t>( ServiceSchedulerInternal::kMaxJobIDSize ) || definition._leaseMs <= 0;
        if ( bInvalidJobDefinition )
            return false;
        for ( const utf8 ch : definition._jobID )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
            if ( bAllowed == false )
                return false;
        }
        switch ( definition._kind )
        {
            case ScheduleKind::Daily:
                return 0 <= definition._minuteOfDay && definition._minuteOfDay < 24 * 60;
            case ScheduleKind::Weekly:
                return 0 <= definition._minuteOfDay && definition._minuteOfDay < 24 * 60 && 0 <= definition._dayOfWeek && definition._dayOfWeek < 7;
            case ScheduleKind::Window:
                return 0 <= definition._windowStartMs && definition._windowStartMs < definition._windowEndMs;
            case ScheduleKind::Interval:
                return definition._intervalMs >= ServiceSchedulerInternal::kMinuteMs;
        }
        return false;
    }

    const hashed_string& ServiceScheduler::getTable()
    {
        static const hashed_string s_table{ "service_schedule" };
        return s_table;
    }

    void ServiceScheduler::onClaimCompleted( size_t jobIndex, int64 occurrenceMs, ClaimResult outcome, uint64 runToken, int64 retryAtMs )
    {
        if ( jobIndex >= _listJob.size() )
            return; // 종료한 뒤에 회수한 일
        Job& job           = _listJob[jobIndex];
        job._bClaimPending = SW_FALSE;
        switch ( outcome )
        {
            case ClaimResult::Claimed:
            {
                job._handledOccurrenceMs = occurrenceMs;
                ScheduledRun run;
                run._jobID        = job._definition._jobID;
                run._occurrenceMs = UnixMilliseconds{ occurrenceMs }; // 회차는 `update` 가 받은 유닉스 시각에서 계산했다(`computeLatestOccurrence`)
                run._runToken     = runToken;
                job._function( run );
                break;
            }
            case ClaimResult::Finished:
            {
                job._handledOccurrenceMs = occurrenceMs;
                break;
            }
            case ClaimResult::Busy:
            case ClaimResult::Retry:
            {
                job._retryOccurrenceMs = occurrenceMs;
                job._retryAtMs         = retryAtMs;
                break;
            }
        }
    }
} // namespace sw
