#include "pch.h"

#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

namespace sw
{
    SW_LOG_CALLER( "ServiceScheduler" );

    namespace
    {
        struct ServiceSchedulerInternal
        {
            static constexpr int64  kMinuteMs       = 60 * 1000;
            static constexpr int64  kDayMs          = 24 * 60 * kMinuteMs;
            static constexpr int64  kDaysPerWeek    = 7;
            static constexpr int64  kEpochWeekday   = 3; ///< 1970-01-01 은 목요일(ISO 월요일 = 0)
            static constexpr int64  kRetryMs        = 5000;
            static constexpr int32  kMaxJobIdSize   = 128;
            static constexpr uint64 kVersion        = 1;
            static constexpr uint32 kStateRunning   = 0;
            static constexpr uint32 kStateSucceeded = 1;
            static constexpr uint32 kStateFailed    = 2;
            static constexpr int32  kStateBitCount  = 2;

            /** @brief 차지 레코드입니다. */
            struct RunRecord
            {
                uint64 _serverId{ 0 };
                int64  _startMs{ 0 };
                int64  _endMs{ 0 };
                uint32 _state{ kStateRunning };
            };

            static string makeKey( string_view jobId, int64 occurrenceMs )
            {
                string key{ jobId };
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, static_cast<uint64>( occurrenceMs ) );
                return key;
            }

            static vector<uint8> encode( const RunRecord& record )
            {
                BitWriter writer;
                writer.writeVarUint( kVersion );
                writer.writeVarUint( record._serverId );
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
                outRecord._serverId = reader.readVarUint();
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
    /** @brief 회차 하나를 차지해 본다(저장소 스레드), 결과는 스케줄러에(서비스 스레드). */
    class ServiceScheduler::ClaimWork final : public IServiceStoreWork
    {
    public:
        ClaimWork( ServiceScheduler* pScheduler, size_t jobIndex, string jobId, int64 occurrenceMs, uint64 serverId, int64 nowMs, int64 leaseMs )
            : _jobId{ std::move( jobId ) }
            , _pScheduler{ pScheduler }
            , _jobIndex{ jobIndex }
            , _occurrenceMs{ occurrenceMs }
            , _serverId{ serverId }
            , _nowMs{ nowMs }
            , _leaseMs{ leaseMs }
            , _runToken{ 0 }
            , _retryAtMs{ 0 }
            , _outcome{ ClaimOutcome::Retry }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            _retryAtMs                   = _nowMs + ServiceSchedulerInternal::kRetryMs;
            const string             key = ServiceSchedulerInternal::makeKey( _jobId, _occurrenceMs );
            ServiceRecord            record;
            const ServiceStoreResult readResult = connection.readRecord( getTable(), key, record );
            if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
                return;
            uint64 expectedVersion = ServiceRecord::kAbsentVersion;
            if ( readResult == ServiceStoreResult::Ok )
            {
                ServiceSchedulerInternal::RunRecord existing;
                if ( ServiceSchedulerInternal::decode( record._bytes, existing ) == false )
                    return; // 깨진 레코드 — 다시 보지 않으면 영영 안 돈다. 운영이 고칠 때까지 다시 해 본다
                if ( existing._state != ServiceSchedulerInternal::kStateRunning )
                {
                    _outcome = ClaimOutcome::Finished;
                    return;
                }
                const int64 leaseEndMs = existing._startMs + _leaseMs;
                if ( _nowMs < leaseEndMs )
                {
                    _outcome   = ClaimOutcome::Busy;
                    _retryAtMs = leaseEndMs;
                    return;
                }
                expectedVersion = record._version; // 임대가 지났다 — 이어받는다
            }
            ServiceSchedulerInternal::RunRecord claim;
            claim._serverId = _serverId;
            claim._startMs  = _nowMs;
            ServiceTransaction transaction;
            transaction.put( getTable(), key, ServiceSchedulerInternal::encode( claim ), expectedVersion );
            ServiceCommitInfo        info;
            const ServiceStoreResult commitResult = connection.commit( transaction, &info );
            if ( commitResult == ServiceStoreResult::Ok )
            {
                _outcome  = ClaimOutcome::Claimed;
                _runToken = info._commitVersion;
                return;
            }
            if ( commitResult == ServiceStoreResult::Conflict )
            {
                _outcome   = ClaimOutcome::Busy; // 같은 순간 다른 서버가 차지했다
                _retryAtMs = _nowMs + _leaseMs;
            }
        }

        void complete() override
        {
            _pScheduler->onWorkCompleted();
            _pScheduler->onClaimCompleted( _jobIndex, _occurrenceMs, _outcome, _runToken, _retryAtMs );
        }

    private:
        string            _jobId;
        ServiceScheduler* _pScheduler;
        size_t            _jobIndex;
        int64             _occurrenceMs;
        uint64            _serverId;
        int64             _nowMs;
        int64             _leaseMs;
        uint64            _runToken;
        int64             _retryAtMs;
        ClaimOutcome      _outcome;
    };
} // namespace sw

namespace sw
{
    /** @brief 차지한 회차의 끝을 적는다(판 조건 — 이어받긴 레코드는 건드리지 않는다). */
    class ServiceScheduler::FinishWork final : public IServiceStoreWork
    {
    public:
        FinishWork( ServiceScheduler* pScheduler, const ScheduledRun& run, uint64 serverId, bool bSucceeded, int64 nowMs )
            : _run{ run }
            , _pScheduler{ pScheduler }
            , _serverId{ serverId }
            , _nowMs{ nowMs }
            , _result{ ServiceStoreResult::Ok }
            , _bSucceeded{ static_cast<uint8>( bSucceeded ? SW_TRUE : SW_FALSE ) }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            ServiceSchedulerInternal::RunRecord finished;
            finished._serverId = _serverId;
            finished._startMs  = _nowMs;
            finished._endMs    = _nowMs;
            finished._state    = _bSucceeded == SW_TRUE ? ServiceSchedulerInternal::kStateSucceeded : ServiceSchedulerInternal::kStateFailed;
            ServiceTransaction transaction;
            transaction.put( getTable(), ServiceSchedulerInternal::makeKey( _run._jobId, _run._occurrenceMs ), ServiceSchedulerInternal::encode( finished ), _run._runToken );
            _result = connection.commit( transaction );
        }

        void complete() override
        {
            _pScheduler->onWorkCompleted();
            if ( _result != ServiceStoreResult::Ok )
                SW_LOG_WARNING( "Could not record the end of scheduled run '%#' (another server took it over or the store is unavailable)", _run._jobId );
        }

    private:
        ScheduledRun       _run;
        ServiceScheduler*  _pScheduler;
        uint64             _serverId;
        int64              _nowMs;
        ServiceStoreResult _result;
        uint8              _bSucceeded;
    };
} // namespace sw

namespace sw
{
    ServiceScheduler::ServiceScheduler()
        : _listJob{}
        , _pStore{ nullptr }
        , _serverId{ 0 }
        , _lastTickMs{ 0 }
        , _pendingWorkCount{ 0 }
    {
    }

    ServiceScheduler::~ServiceScheduler() { shutdown(); }

    void ServiceScheduler::initialize( IServiceStore* pStore, uint64 serverId )
    {
        _pStore   = pStore;
        _serverId = serverId;
        _listJob.clear();
    }

    void ServiceScheduler::shutdown()
    {
        _listJob.clear();
        _pStore = nullptr;
    }

    bool ServiceScheduler::registerJob( const ScheduleDefinition& definition, IScheduledJobHandler* pHandler )
    {
        if ( pHandler == nullptr || isValidDefinition( definition ) == false )
        {
            SW_LOG_WARNING( "Ignored scheduled job '%#' — invalid schedule or no handler", definition._jobId );
            return false;
        }
        Job& job        = _listJob.emplace_back();
        job._definition = definition;
        job._pHandler   = pHandler;
        return true;
    }

    void ServiceScheduler::tick( int64 nowMs )
    {
        if ( _pStore == nullptr )
            return;
        _lastTickMs = nowMs;
        for ( size_t jobIndex = 0; jobIndex < _listJob.size(); ++jobIndex )
        {
            Job& job = _listJob[jobIndex];
            if ( job._bClaimPending == SW_TRUE )
                continue;
            const int64 occurrenceMs = computeLatestOccurrence( job._definition, nowMs );
            if ( occurrenceMs < 0 || occurrenceMs <= job._handledOccurrenceMs )
                continue;
            const bool bWaitingRetry = job._retryOccurrenceMs == occurrenceMs && nowMs < job._retryAtMs;
            if ( bWaitingRetry )
                continue;
            job._bClaimPending = SW_TRUE;
            ++_pendingWorkCount;
            _pStore->submit( sw::make_unique<ClaimWork>( this, jobIndex, job._definition._jobId, occurrenceMs, _serverId, nowMs, job._definition._leaseMs ) );
        }
    }

    void ServiceScheduler::completeRun( const ScheduledRun& run, bool bSucceeded )
    {
        if ( _pStore == nullptr )
            return;
        ++_pendingWorkCount;
        _pStore->submit( sw::make_unique<FinishWork>( this, run, _serverId, bSucceeded, _lastTickMs ) );
    }

    int64 ServiceScheduler::computeLatestOccurrence( const ScheduleDefinition& definition, int64 nowMs )
    {
        if ( nowMs < 0 )
            return -1;
        const int64 minuteOffsetMs = static_cast<int64>( definition._minuteOfDay ) * ServiceSchedulerInternal::kMinuteMs;
        const int64 dayIndex       = ServiceSchedulerInternal::floorDivide( nowMs, ServiceSchedulerInternal::kDayMs );
        int64       occurrenceMs   = -1;
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
                occurrenceMs       = bInside ? definition._windowStartMs : -1;
                break;
            }
        }
        return occurrenceMs >= 0 ? occurrenceMs : -1;
    }

    bool ServiceScheduler::isValidDefinition( const ScheduleDefinition& definition )
    {
        if ( definition._jobId.empty() || definition._jobId.size() > static_cast<size_t>( ServiceSchedulerInternal::kMaxJobIdSize ) || definition._leaseMs <= 0 )
            return false;
        for ( const utf8 ch : definition._jobId )
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
        }
        return false;
    }

    const hashed_string& ServiceScheduler::getTable()
    {
        static const hashed_string s_table{ "service_schedule" };
        return s_table;
    }

    void ServiceScheduler::onClaimCompleted( size_t jobIndex, int64 occurrenceMs, ClaimOutcome outcome, uint64 runToken, int64 retryAtMs )
    {
        if ( jobIndex >= _listJob.size() )
            return; // 내린 뒤에 거둔 일
        Job& job           = _listJob[jobIndex];
        job._bClaimPending = SW_FALSE;
        switch ( outcome )
        {
            case ClaimOutcome::Claimed:
            {
                job._handledOccurrenceMs = occurrenceMs;
                ScheduledRun run;
                run._jobId        = job._definition._jobId;
                run._occurrenceMs = occurrenceMs;
                run._runToken     = runToken;
                job._pHandler->onScheduledRun( run );
                break;
            }
            case ClaimOutcome::Finished:
            {
                job._handledOccurrenceMs = occurrenceMs;
                break;
            }
            case ClaimOutcome::Busy:
            case ClaimOutcome::Retry:
            {
                job._retryOccurrenceMs = occurrenceMs;
                job._retryAtMs         = retryAtMs;
                break;
            }
        }
    }
} // namespace sw
