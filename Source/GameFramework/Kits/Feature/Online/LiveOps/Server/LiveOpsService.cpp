#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsProtocol.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveEventRules.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>
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

            static uint64 combineText( uint64 hash, string_view text ) { return HashUtil::combine( hash, HashUtil::computeStringHash64( text.data(), text.size(), false ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 이벤트 테이블을 읽습니다. 손상된 영속 레코드는 건너뛰지 않는다 — 이 읽기를 실패로 두어 이전 값을 지키고 오류 로그를 남긴다. */
    struct LiveOpsStoreRun
    {
        static ServiceStoreRead<vector<LiveEventDef>> readEvents( IServiceStoreConnection& connection )
        {
            ServiceStoreRead<vector<LiveEventDef>> read;
            vector<ServiceRecord>                  listRecord;
            read._result = connection.listRecords( LiveOpsServiceInternal::getEventTable(), "", "", LiveOpsLimit::kMaxEventCount, false, listRecord );
            if ( read._result != ServiceStoreResult::Ok )
                return read;
            for ( const ServiceRecord& record : listRecord )
            {
                LiveEventDef definition;
                if ( LiveOpsProtocol::decodeEvent( record._key, record._bytes, definition ) == false )
                {
                    read._result = ServiceStoreResult::Invalid;
                    return read;
                }
                // 이벤트 정의는 고정 배치형 코덱이 없어 상한을 읽기가 보지 않으므로, 운영 쓰기와 같은 규칙으로 다시 확인한다
                if ( LiveEventRules::isValid( definition ) == false )
                {
                    SW_LOG_ERROR_ID( "LiveOpsService.LiveOpsEventRecordBreaks", "LiveOps event record '%#' breaks the event rules", record._key );
                    read._result = ServiceStoreResult::Invalid;
                    return read;
                }
                read._value.push_back( std::move( definition ) );
            }
            return read;
        }
    };
} // namespace sw

namespace sw
{
    LiveOpsService::LiveOpsService()
        : _listEvent{}
        , _listOpenWindow{}
        , _listStarted{}
        , _completionBuffer{}
        , _dependencies{}
        , _lastOpenHash{ 0 }
        , _reloadJob{}
        , _store{}
        , _bStateChanged{ SW_FALSE }
    {
    }

    LiveOpsService::~LiveOpsService()
    {
        shutdown();
    }

    void LiveOpsService::initialize( const LiveOpsDependencies& dependencies )
    {
        SW_ASSERT( dependencies._pStore != nullptr );
        _dependencies = dependencies;
        _store.bindStore( dependencies._pStore );
        _reloadJob.setPeriod( LiveOpsBus::kReloadPeriodMs );      // 첫 틱에 한 번 읽는다
        _lastOpenHash = computeOpenHash( UnixMilliseconds{ 0 } ); // 빈 상태는 알리지 않는다
    }

    void LiveOpsService::shutdown()
    {
        _store.bindStore( nullptr ); // 남은 일은 조립이 저장소 드레인으로 거둔다(예전과 같이 여기서 기다리지 않는다)
        _dependencies._pBus = nullptr;
    }

    void LiveOpsService::update( UnixMilliseconds nowMs )
    {
        if ( _store.hasStore() == false )
            return;
        if ( _reloadJob.tryStart( nowMs ) )
            startReload( nowMs );
        markIfChanged( nowMs ); // 시작, 끝, 회차 경계는 다시 읽지 않아도 열린 이벤트 목록이 바뀐다
    }

    void LiveOpsService::startReload( UnixMilliseconds nowMs )
    {
        _store.submit( []( IServiceStoreConnection& connection )
        { return LiveOpsStoreRun::readEvents( connection ); },
                       [this, nowMs]( ServiceStoreRead<vector<LiveEventDef>>& read )
        { applyReload( std::move( read._value ), read.isOk(), nowMs ); } );
    }

    void LiveOpsService::applyReload( vector<LiveEventDef>&& listEvent, bool bReadOk, UnixMilliseconds nowMs )
    {
        _reloadJob.finish();
        if ( bReadOk == false )
            return; // 저장소에 문제가 있으므로 이전 값을 두고 다음 주기에 다시 읽는다
        _listEvent = std::move( listEvent );
        markIfChanged( nowMs );
    }

    bool LiveOpsService::takeStateChange()
    {
        const bool bChanged = _bStateChanged == SW_TRUE;
        _bStateChanged      = SW_FALSE;
        return bChanged;
    }

    void LiveOpsService::takeStartedEvents( vector<LiveEventStart>& outListStart )
    {
        for ( LiveEventStart& start : _listStarted )
        {
            outListStart.push_back( std::move( start ) );
        }
        _listStarted.clear();
    }

    void LiveOpsService::collectStarts( UnixMilliseconds nowMs )
    {
        vector<OpenWindow> listOpen;
        if ( isKillSwitchOff() == false )
        {
            for ( const LiveEventDef& definition : _listEvent )
            {
                int64 windowStartMs = 0;
                int64 windowEndMs   = 0;
                if ( LiveEventRules::findOpenWindow( definition, nowMs.count(), windowStartMs, windowEndMs ) == false )
                    continue;
                listOpen.push_back( OpenWindow{ definition._eventID, windowEndMs } );
                const auto isSameWindow = [&definition, windowEndMs]( const OpenWindow& window )
                { return window._eventID == definition._eventID && window._windowEndMs == windowEndMs; };
                const bool bKnown      = VectorUtil::containsIf( _listOpenWindow, isSameWindow );
                const bool bJustOpened = nowMs.count() - windowStartMs <= LiveOpsPush::kStartGraceMs;
                if ( bKnown || bJustOpened == false )
                    continue;
                PushNotificationMessage message;
                (void)findParameter( definition._eventID, LiveOpsPush::kTitleParameter, message._titleKey ); // 선택 값이다 — 없으면 비우고 아래가 거른다
                (void)findParameter( definition._eventID, LiveOpsPush::kBodyParameter, message._bodyKey );   // 선택 값이다 — 없으면 비우고 아래가 거른다
                (void)findParameter( definition._eventID, LiveOpsPush::kLinkParameter, message._deepLink );  // 선택 값이다 — 없으면 비우고 아래가 거른다
                if ( message._titleKey.empty() && message._bodyKey.empty() )
                    continue; // 시작 푸시를 달지 않은 이벤트이다
                LiveEventStart& start = _listStarted.emplace_back();
                start._message        = std::move( message );
                start._listRegion     = definition._listRegion;
                start._eventID        = definition._eventID;
                start._windowEndMs    = windowEndMs;
            }
        }
        _listOpenWindow = std::move( listOpen );
    }

    void LiveOpsService::markIfChanged( UnixMilliseconds nowMs )
    {
        collectStarts( nowMs );
        const uint64 hash = computeOpenHash( nowMs );
        if ( hash == _lastOpenHash )
            return;
        _lastOpenHash  = hash;
        _bStateChanged = SW_TRUE;
    }

    uint64 LiveOpsService::computeOpenHash( UnixMilliseconds nowMs ) const
    {
        using Internal = LiveOpsServiceInternal;
        uint64 hash    = HashUtil::kFNVOffset64;
        if ( isKillSwitchOff() )
            return hash;
        if ( _dependencies._pRemoteConfig != nullptr )
            hash = HashUtil::combine( hash, _dependencies._pRemoteConfig->getSnapshotHash() ); // '@' 값이 바뀌어도 클라이언트가 다시 받게 한다
        for ( const LiveEventDef& definition : _listEvent )
        {
            int64 windowEndMs = 0;
            if ( LiveEventRules::isWindowOpen( definition, nowMs.count(), windowEndMs ) == false )
                continue;
            hash = Internal::combineText( hash, definition._eventID );
            hash = HashUtil::combine( hash, static_cast<uint64>( windowEndMs ) ); // 반복 회차가 바뀌어도 해시가 바뀐다
            for ( const LiveEventParameter& parameter : definition._listParameter )
            {
                hash = Internal::combineText( Internal::combineText( hash, parameter._key ), parameter._value );
            }
        }
        return hash;
    }

    bool LiveOpsService::isKillSwitchOff() const
    {
        return _dependencies._pRemoteConfig != nullptr && _dependencies._pRemoteConfig->isFeatureEnabled( LiveOpsBus::kKillSwitchFlag, 0, true ) == false;
    }

    string LiveOpsService::resolveValue( const string& value ) const
    {
        string     text;
        const bool bLiteralValue = value.empty() || value.front() != '@' || _dependencies._pRemoteConfig == nullptr;
        if ( bLiteralValue )
        {
            text = value;
            return text;
        }
        const string_view key( value.data() + 1, value.size() - 1 );
        if ( _dependencies._pRemoteConfig->findText( key, text ) )
            return text;
        int64 integer = 0;
        if ( _dependencies._pRemoteConfig->findInteger( key, integer ) )
        {
            text = string( std::to_string( integer ).c_str() );
            return text;
        }
        SW_LOG_WARNING_ID( "LiveOpsService.LiveOpsRemoteConfigKey", "LiveOps: remote config key '%#' not found — parameter left as is", value.c_str() );
        text = value;
        return text;
    }

    const LiveEventDef* LiveOpsService::findEvent( string_view eventID ) const
    {
        return VectorUtil::findBy<&LiveEventDef::_eventID>( _listEvent, eventID );
    }

    void LiveOpsService::computeActiveEvents( AccountID accountID, string_view region, uint32 buildVersion, UnixMilliseconds nowMs, bool bClientOnly,
                                              vector<LiveEventState>& outListEvent ) const
    {
        if ( isKillSwitchOff() )
            return;
        for ( const LiveEventDef& definition : _listEvent )
        {
            int64 windowEndMs = 0;
            if ( bClientOnly && definition._bClientVisible == SW_FALSE )
                continue;
            if ( LiveEventRules::isWindowOpen( definition, nowMs.count(), windowEndMs ) == false || LiveEventRules::isAudienceMatch( definition, accountID, region, buildVersion ) == false )
                continue;
            LiveEventState& state = outListEvent.emplace_back();
            state._eventID        = definition._eventID;
            state._kind           = definition._kind;
            state._windowEndMs    = windowEndMs;
            for ( const LiveEventParameter& parameter : definition._listParameter )
            {
                state._listParameter.push_back( LiveEventParameter{ parameter._key, resolveValue( parameter._value ) } );
            }
        }
    }

    bool LiveOpsService::isEventActive( string_view eventID, AccountID accountID, string_view region, uint32 buildVersion, UnixMilliseconds nowMs ) const
    {
        const LiveEventDef* pDefinition = findEvent( eventID );
        int64               windowEndMs = 0;
        return pDefinition != nullptr && isKillSwitchOff() == false && LiveEventRules::isWindowOpen( *pDefinition, nowMs.count(), windowEndMs ) &&
               LiveEventRules::isAudienceMatch( *pDefinition, accountID, region, buildVersion );
    }

    bool LiveOpsService::findParameter( string_view eventID, string_view key, string& outValue ) const
    {
        const LiveEventDef* pDefinition = findEvent( eventID );
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

    void LiveOpsService::putEvent( const LiveEventDef& definition, AccountID actorID, string_view memo, UnixMilliseconds nowMs, uint64 requestTag )
    {
        if ( LiveEventRules::isValid( definition ) == false )
        {
            _completionBuffer.push( LiveOpsCompletion{ requestTag, LiveOpsServiceResult::fail( OnlineError::kInvalidRequest ) } );
            return;
        }
        ServiceAuditedWrite pendingJob;
        if ( LiveOpsProtocol::encodeEvent( definition, pendingJob._bytes ) == false )
        {
            _completionBuffer.push( LiveOpsCompletion{ requestTag, LiveOpsServiceResult::fail( OnlineError::kInvalidRequest ) } ); // 쓰기가 오류 로그를 남겼다
            return;
        }
        pendingJob._key                 = definition._eventID;
        pendingJob._auditEntry._actor   = ServiceAuditLog::makeOperatorActor( actorID );
        pendingJob._auditEntry._action  = "liveops.event.put";
        pendingJob._auditEntry._subject = "liveops/" + definition._eventID;
        pendingJob._auditEntry._memo    = string( memo );
        pendingJob._auditEntry._timeMs  = nowMs.count();
        submitWrite( std::move( pendingJob ), requestTag );
    }

    void LiveOpsService::putEventJSON( string_view definitionJSON, AccountID actorID, string_view memo, UnixMilliseconds nowMs, uint64 requestTag )
    {
        LiveEventDef definition;
        if ( LiveOpsProtocol::readEventJSON( definitionJSON, definition ) == false )
        {
            _completionBuffer.push( LiveOpsCompletion{ requestTag, LiveOpsServiceResult::fail( OnlineError::kInvalidRequest ) } ); // JSON 이 아니거나 모르는 필드가 있다
            return;
        }
        putEvent( definition, actorID, memo, nowMs, requestTag ); // 규칙 검사는 같은 길(`LiveEventRules::isValid`)이다
    }

    void LiveOpsService::collectEventJSON( vector<string>& outListJSON ) const
    {
        outListJSON.clear();
        outListJSON.reserve( _listEvent.size() );
        for ( const LiveEventDef& definition : _listEvent )
        {
            expected<string> text = LiveOpsProtocol::writeEventJSON( definition );
            if ( text.has_value() == false )
            {
                SW_LOG_ERROR_ID( "LiveOpsService.EventNotWritable", "Live event '%#' could not be written as JSON: %# (%#) - left out of the list", definition._eventID, text.error().message(),
                                 text.error().detail() );
                continue;
            }
            outListJSON.push_back( std::move( *text ) );
        }
    }

    void LiveOpsService::takeControlCompletions( vector<LiveEventControlCompletion>& outListCompletion )
    {
        vector<LiveOpsCompletion> listCompletion;
        _completionBuffer.moveAllTo( listCompletion );
        for ( const LiveOpsCompletion& completion : listCompletion )
        {
            LiveEventControlResult result = LiveEventControlResult::Unavailable;
            if ( completion._result == LiveOpsResult::Ok )
                result = LiveEventControlResult::Ok;
            else if ( completion._result.hasError( OnlineError::kInvalidRequest ) )
                result = LiveEventControlResult::Invalid;
            else if ( completion._result == LiveOpsResult::NotFound )
                result = LiveEventControlResult::NotFound;
            outListCompletion.push_back( LiveEventControlCompletion{ completion._requestTag, result } );
        }
    }

    void LiveOpsService::removeEvent( string_view eventID, AccountID actorID, string_view memo, UnixMilliseconds nowMs, uint64 requestTag )
    {
        if ( LiveOpsLimit::isValidKey( eventID, LiveOpsLimit::kMaxIDSize ) == false )
        {
            _completionBuffer.push( LiveOpsCompletion{ requestTag, LiveOpsServiceResult::fail( OnlineError::kInvalidRequest ) } );
            return;
        }
        ServiceAuditedWrite pendingJob;
        pendingJob._key                 = string( eventID );
        pendingJob._auditEntry._actor   = ServiceAuditLog::makeOperatorActor( actorID );
        pendingJob._auditEntry._action  = "liveops.event.remove";
        pendingJob._auditEntry._subject = "liveops/" + pendingJob._key;
        pendingJob._auditEntry._memo    = string( memo );
        pendingJob._auditEntry._timeMs  = nowMs.count();
        submitWrite( std::move( pendingJob ), requestTag );
    }

    void LiveOpsService::submitWrite( ServiceAuditedWrite&& pendingJob, uint64 requestTag )
    {
        pendingJob._table = LiveOpsServiceInternal::getEventTable();
        _store.submit( [pendingJob = std::move( pendingJob )]( IServiceStoreConnection& connection )
        { return pendingJob.commit( connection ); },
                       [this, requestTag]( ServiceStoreResult result )
        {
            const LiveOpsServiceResult serviceResult = result == ServiceStoreResult::Ok       ? LiveOpsServiceResult{ LiveOpsResult::Ok }
                                                     : result == ServiceStoreResult::NotFound ? LiveOpsServiceResult{ LiveOpsResult::NotFound }
                                                                                              : LiveOpsServiceResult::fail( OnlineError::kUnavailable );
            applyWrite( requestTag, serviceResult );
        } );
    }

    void LiveOpsService::applyWrite( uint64 requestTag, LiveOpsServiceResult result )
    {
        _completionBuffer.push( LiveOpsCompletion{ requestTag, result } );
        if ( result != LiveOpsResult::Ok )
            return;
        _reloadJob.request(); // 이 서버는 다음 tick 에 다시 읽고 알린다
        if ( _dependencies._pBus != nullptr )
            (void)LiveOpsBus::kChangedTopic.publish( *_dependencies._pBus, LiveOpsChanged{} ); // 실패는 발행이 로그로 남겼다. 다른 서버는 주기로 다시 읽는다
    }
} // namespace sw
