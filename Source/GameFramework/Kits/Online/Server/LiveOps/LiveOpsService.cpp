#include "pch.h"

#include "GameFramework/Kits/Online/Server/LiveOps/LiveOpsService.h"

#include "Core/Common/HashUtil.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Online/LiveOps/LiveOpsProtocol.h"
#include "GameFramework/Kits/Online/Server/LiveOps/LiveEventRules.h"

#include <string>

namespace sw
{
    namespace
    {
        struct LiveOpsServiceInternal
        {
            static const hashed_string& getEventTable()
            {
                static const hashed_string s_table{ "liveops_event" };
                return s_table;
            }

            static string makeActor( AccountId actorId ) { return actorId == kInvalidAccountId ? string( "system" ) : "gm." + ServiceKeyUtil::makeHex64( actorId ); }

            static uint64 combineText( uint64 hash, string_view text ) { return HashUtil::combine( hash, StringUtil::computeHash64( text.data(), text.size(), false ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 이벤트 표를 읽는 일입니다. */
    class LiveOpsReloadWork final : public IServiceStoreWork
    {
    public:
        LiveOpsReloadWork( LiveOpsService* pService, int64 nowMs )
            : _listEvent{}
            , _pService{ pService }
            , _nowMs{ nowMs }
            , _bReadOk{ SW_FALSE }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            vector<ServiceRecord> listRecord;
            if ( connection.listRecords( LiveOpsServiceInternal::getEventTable(), "", "", LiveOpsLimit::kMaxEventCount, false, listRecord ) != ServiceStoreResult::Ok )
                return;
            for ( const ServiceRecord& record : listRecord )
            {
                LiveEventDefinition definition;
                if ( LiveOpsProtocol::decodeEvent( record._bytes, definition ) )
                    _listEvent.push_back( std::move( definition ) );
            }
            _bReadOk = SW_TRUE;
        }

        void complete() override { _pService->applyReload( std::move( _listEvent ), _bReadOk == SW_TRUE, _nowMs ); }

    private:
        vector<LiveEventDefinition> _listEvent;
        LiveOpsService*             _pService;
        int64                       _nowMs;
        uint8                       _bReadOk;
    };
} // namespace sw

namespace sw
{
    /** @brief 이벤트 하나 쓰기 · 지우기 + 감사 줄을 한 트랜잭션으로(판 조건 — 그새 바뀌었으면 Unavailable, 다시 하면 된다). */
    class LiveOpsWriteWork final : public IServiceStoreWork
    {
    public:
        ServiceAuditEntry _auditEntry;
        vector<uint8>     _bytes; ///< 비면 지우기
        string            _key;
        NetIdempotencyKey _auditUnique;
        LiveOpsService*   _pService;
        uint64            _requestTag;
        LiveOpsResult     _result;

        LiveOpsWriteWork()
            : _auditEntry{}
            , _bytes{}
            , _key{}
            , _auditUnique{ NetIdempotencyKey::makeRandom() }
            , _pService{ nullptr }
            , _requestTag{ 0 }
            , _result{ LiveOpsResult::Unavailable }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            const hashed_string&     table = LiveOpsServiceInternal::getEventTable();
            ServiceRecord            current;
            const ServiceStoreResult readResult = connection.readRecord( table, _key, current );
            if ( readResult == ServiceStoreResult::Unavailable || readResult == ServiceStoreResult::Invalid )
                return;
            if ( _bytes.empty() && readResult == ServiceStoreResult::NotFound )
            {
                _result = LiveOpsResult::NotFound;
                return;
            }
            ServiceTransaction transaction;
            if ( _bytes.empty() )
                transaction.erase( table, _key, current._version );
            else
                transaction.put( table, _key, _bytes, current._version );
            ServiceAuditLog::stageEntry( transaction, _auditEntry, _auditUnique._high, _auditUnique._low );
            _result = connection.commit( transaction ) == ServiceStoreResult::Ok ? LiveOpsResult::Ok : LiveOpsResult::Unavailable; // Conflict 도 "다시 하라"
        }

        void complete() override { _pService->applyWrite( _requestTag, _result ); }
    };
} // namespace sw

namespace sw
{
    LiveOpsService::LiveOpsService()
        : _listEvent{}
        , _completionBuffer{}
        , _dependencies{}
        , _lastReloadMs{ 0 }
        , _lastOpenHash{ 0 }
        , _pendingCount{ 0 }
        , _bReloading{ SW_FALSE }
        , _bReloadRequested{ SW_TRUE }
        , _bStateChanged{ SW_FALSE }
    {
    }

    LiveOpsService::~LiveOpsService() { shutdown(); }

    void LiveOpsService::initialize( const LiveOpsDependencies& dependencies )
    {
        SW_ASSERT( dependencies._pStore != nullptr );
        _dependencies     = dependencies;
        _bReloadRequested = SW_TRUE;
        _lastOpenHash     = computeOpenHash( 0 ); // 빈 상태는 알리지 않는다
    }

    void LiveOpsService::shutdown() { _dependencies._pBus = nullptr; }

    void LiveOpsService::tick( int64 nowMs )
    {
        if ( _dependencies._pStore == nullptr )
            return;
        const bool bPeriodic = nowMs - _lastReloadMs >= LiveOpsBus::kReloadPeriodMs;
        if ( _bReloading == SW_FALSE && ( _bReloadRequested == SW_TRUE || bPeriodic ) )
            startReload( nowMs );
        markIfChanged( nowMs ); // 시작 · 끝 · 회차 경계는 다시 읽지 않아도 열린 묶음이 바뀐다
    }

    void LiveOpsService::startReload( int64 nowMs )
    {
        _bReloadRequested = SW_FALSE;
        _bReloading       = SW_TRUE;
        _lastReloadMs     = nowMs;
        ++_pendingCount;
        _dependencies._pStore->submit( make_unique<LiveOpsReloadWork>( this, nowMs ) );
    }

    void LiveOpsService::applyReload( vector<LiveEventDefinition>&& listEvent, bool bReadOk, int64 nowMs )
    {
        --_pendingCount;
        _bReloading = SW_FALSE;
        if ( bReadOk == false )
            return; // 저장소가 아프다 — 옛 것을 두고 다음 주기에
        _listEvent = std::move( listEvent );
        markIfChanged( nowMs );
    }

    bool LiveOpsService::takeStateChange()
    {
        const bool bChanged = _bStateChanged == SW_TRUE;
        _bStateChanged      = SW_FALSE;
        return bChanged;
    }

    void LiveOpsService::markIfChanged( int64 nowMs )
    {
        const uint64 hash = computeOpenHash( nowMs );
        if ( hash == _lastOpenHash )
            return;
        _lastOpenHash  = hash;
        _bStateChanged = SW_TRUE;
    }

    uint64 LiveOpsService::computeOpenHash( int64 nowMs ) const
    {
        using Internal = LiveOpsServiceInternal;
        uint64 hash    = HashUtil::kFnvOffset64;
        if ( isKillSwitchOff() )
            return hash;
        if ( _dependencies._pRemoteConfig != nullptr )
            hash = HashUtil::combine( hash, _dependencies._pRemoteConfig->getSnapshotHash() ); // '@' 값이 바뀌어도 다시 받게
        for ( const LiveEventDefinition& definition : _listEvent )
        {
            int64 windowEndMs = 0;
            if ( LiveEventRules::isWindowOpen( definition, nowMs, windowEndMs ) == false )
                continue;
            hash = Internal::combineText( hash, definition._eventId );
            hash = HashUtil::combine( hash, static_cast<uint64>( windowEndMs ) ); // 반복 회차가 바뀌어도 바뀐다
            for ( const LiveEventParameter& parameter : definition._listParameter )
                hash = Internal::combineText( Internal::combineText( hash, parameter._key ), parameter._value );
        }
        return hash;
    }

    bool LiveOpsService::isKillSwitchOff() const
    {
        return _dependencies._pRemoteConfig != nullptr && _dependencies._pRemoteConfig->isFeatureEnabled( LiveOpsBus::kKillSwitchFlag, 0, true ) == false;
    }

    string LiveOpsService::resolveValue( const string& value ) const
    {
        if ( value.empty() || value.front() != '@' || _dependencies._pRemoteConfig == nullptr )
            return value;
        const string_view key( value.data() + 1, value.size() - 1 );
        string            text;
        if ( _dependencies._pRemoteConfig->findText( key, text ) )
            return text;
        int64 integer = 0;
        if ( _dependencies._pRemoteConfig->findInteger( key, integer ) )
            return string( std::to_string( integer ).c_str() );
        SW_LOG_WARNING( "LiveOps: remote config key '%#' not found — parameter left as is", value.c_str() );
        return value;
    }

    const LiveEventDefinition* LiveOpsService::findEvent( string_view eventId ) const
    {
        for ( const LiveEventDefinition& definition : _listEvent )
        {
            if ( definition._eventId == eventId )
                return &definition;
        }
        return nullptr;
    }

    void LiveOpsService::computeActiveEvents( AccountId accountId, string_view region, uint32 buildVersion, int64 nowMs, bool bClientOnly,
                                              vector<LiveEventState>& outListEvent ) const
    {
        if ( isKillSwitchOff() )
            return;
        for ( const LiveEventDefinition& definition : _listEvent )
        {
            int64 windowEndMs = 0;
            if ( bClientOnly && definition._bClientVisible == SW_FALSE )
                continue;
            if ( LiveEventRules::isWindowOpen( definition, nowMs, windowEndMs ) == false || LiveEventRules::isAudienceMatch( definition, accountId, region, buildVersion ) == false )
                continue;
            LiveEventState& state = outListEvent.emplace_back();
            state._eventId        = definition._eventId;
            state._kind           = definition._kind;
            state._windowEndMs    = windowEndMs;
            for ( const LiveEventParameter& parameter : definition._listParameter )
                state._listParameter.push_back( LiveEventParameter{ parameter._key, resolveValue( parameter._value ) } );
        }
    }

    bool LiveOpsService::isEventActive( string_view eventId, AccountId accountId, string_view region, uint32 buildVersion, int64 nowMs ) const
    {
        const LiveEventDefinition* pDefinition = findEvent( eventId );
        int64                      windowEndMs = 0;
        return pDefinition != nullptr && isKillSwitchOff() == false && LiveEventRules::isWindowOpen( *pDefinition, nowMs, windowEndMs ) &&
               LiveEventRules::isAudienceMatch( *pDefinition, accountId, region, buildVersion );
    }

    bool LiveOpsService::findParameter( string_view eventId, string_view key, string& outValue ) const
    {
        const LiveEventDefinition* pDefinition = findEvent( eventId );
        if ( pDefinition == nullptr )
            return false;
        for ( const LiveEventParameter& parameter : pDefinition->_listParameter )
        {
            if ( parameter._key == key )
            {
                outValue = resolveValue( parameter._value );
                return true;
            }
        }
        return false;
    }

    void LiveOpsService::putEvent( const LiveEventDefinition& definition, AccountId actorId, string_view memo, int64 nowMs, uint64 requestTag )
    {
        if ( LiveEventRules::isValid( definition ) == false )
        {
            _completionBuffer.push( LiveOpsCompletion{ requestTag, LiveOpsResult::Invalid } );
            return;
        }
        unique_ptr<LiveOpsWriteWork> work = make_unique<LiveOpsWriteWork>();
        work->_key                        = definition._eventId;
        work->_bytes                      = LiveOpsProtocol::encodeEvent( definition );
        work->_auditEntry._actor          = LiveOpsServiceInternal::makeActor( actorId );
        work->_auditEntry._action         = "liveops.event.put";
        work->_auditEntry._subject        = "liveops/" + definition._eventId;
        work->_auditEntry._memo           = string( memo );
        work->_auditEntry._timeMs         = nowMs;
        work->_requestTag                 = requestTag;
        submitWrite( std::move( work ) );
    }

    void LiveOpsService::removeEvent( string_view eventId, AccountId actorId, string_view memo, int64 nowMs, uint64 requestTag )
    {
        if ( LiveOpsLimit::isValidKey( eventId, LiveOpsLimit::kMaxIdSize ) == false )
        {
            _completionBuffer.push( LiveOpsCompletion{ requestTag, LiveOpsResult::Invalid } );
            return;
        }
        unique_ptr<LiveOpsWriteWork> work = make_unique<LiveOpsWriteWork>();
        work->_key                        = string( eventId );
        work->_auditEntry._actor          = LiveOpsServiceInternal::makeActor( actorId );
        work->_auditEntry._action         = "liveops.event.remove";
        work->_auditEntry._subject        = "liveops/" + work->_key;
        work->_auditEntry._memo           = string( memo );
        work->_auditEntry._timeMs         = nowMs;
        work->_requestTag                 = requestTag;
        submitWrite( std::move( work ) );
    }

    void LiveOpsService::submitWrite( unique_ptr<LiveOpsWriteWork> work )
    {
        work->_pService = this;
        ++_pendingCount;
        _dependencies._pStore->submit( std::move( work ) );
    }

    void LiveOpsService::applyWrite( uint64 requestTag, LiveOpsResult result )
    {
        --_pendingCount;
        _completionBuffer.push( LiveOpsCompletion{ requestTag, result } );
        if ( result != LiveOpsResult::Ok )
            return;
        _bReloadRequested = SW_TRUE; // 이 서버는 다음 tick 에 다시 읽고 알린다
        if ( _dependencies._pBus != nullptr )
            _dependencies._pBus->publish( LiveOpsBus::kChangedTopic, nullptr, 0 );
    }
} // namespace sw
