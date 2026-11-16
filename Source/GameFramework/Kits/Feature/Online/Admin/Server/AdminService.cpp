#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Config/LiveEventControl.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Directory/ServerDirectoryControl.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Ledger/IBalanceChangedListener.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Sanction/Server/ServiceSanction.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    AdminService::AdminService()
        : _localCalls{}
        , _mapCallToExtra{}
        , _settings{}
    {
        setReady( false );
    }

    AdminService::~AdminService()
    {
        SW_ASSERT( _calls.isEmpty() && _localCalls.isEmpty() );
    }

    bool AdminService::initialize( IServiceStore* pStore, const AdminServiceConfig& settings )
    {
        if ( pStore == nullptr )
        {
            SW_LOG_ERROR_ID( "AdminService.AdminServiceNeedsStore", "AdminService needs a store" );
            return false;
        }
        _store.bindStore( pStore );
        _settings = settings;
        initializeCalls( settings._pMetricRegistry, "admin" );
        setReady( true );
        return true;
    }

    void AdminService::shutdown()
    {
        // 넘긴 점검과 공지 명령은 서버 디렉터리 키트의 저장 완료 뒤에 끝나므로 회수할 때마다 그 완료도 회수한다
        _store.flushUntilIdle( [this]()
        { return getOpenJobCount() == 0; }, [this]()
        { pollForwardedCompletions(); } );
        _calls.abandonAll();
        _localCalls.clear();
        _mapCallToExtra.clear();
        setReady( false );
        _store.bindStore( nullptr );
    }

    void AdminService::submitCall( AccountID adminID, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, UnixMilliseconds nowMs, ReplyDelegate onReply )
    {
        startCall( _localCalls.open( onReply ), adminID, method, request, idempotencyKey, nowMs );
    }

    void AdminService::startCall( uint64 callID, AccountID adminID, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, UnixMilliseconds nowMs )
    {
        AdminCallExtra extra;
        extra._nowMs    = nowMs; // 요청의 시각은 필수 인자다 — 마지막 틱 시각으로 메우지 않는다
        extra._targetID = request._accountID;
        extra._adminID  = adminID;
        extra._method   = method;
        if ( AdminMethod::isForwardedMethod( method ) )
            extra._request = request;
        const int32 methodIndex = AdminMethod::toIndex( method );
        _mapCallToExtra[callID] = extra;

        AdminCommand command;
        command._request    = request;
        command._pPolicy    = _settings._pPolicy;
        command._pNameIndex = _settings._pNameIndex;
        command._adminID    = adminID;
        command._keyHigh    = idempotencyKey._high;
        command._keyLow     = idempotencyKey._low;
        command._nowMs      = nowMs.count();
        command._method     = method;

        AdminServiceResult refused{ AdminResult::Ok };
        if ( _store.hasStore() == false )
            refused = AdminServiceResult::fail( OnlineError::kUnavailable );
        else if ( adminID == kInvalidAccountID )
            refused = AdminServiceResult::fail( OnlineError::kUnauthenticated );
        else if ( methodIndex < 0 )
            refused = AdminServiceResult::fail( OnlineError::kForbidden ); // 모르는 명령이므로 누구도 가질 수 없는 등급이 필요하다
        if ( refused != AdminResult::Ok )
        {
            respondCall( callID, refused, AdminReply{} );
            return;
        }
        _store.submit( [command]( IServiceStoreConnection& connection )
        {
            ServiceStoreCallResult<AdminServiceResult, AdminReply> done;
            done._serviceResult = AdminStoreLogic::execute( connection, command, done._reply );
            return done;
        }, [this, callID]( ServiceStoreCallResult<AdminServiceResult, AdminReply>& done )
        { completeCall( callID, done._serviceResult, done._reply ); } );
    }

    void AdminService::seedRole( AccountID accountID, AdminRole role, UnixMilliseconds nowMs )
    {
        const bool bInvalidSeed = _store.hasStore() == false || accountID == kInvalidAccountID || role == AdminRole::None || role >= AdminRole::Count;
        if ( bInvalidSeed )
            return;
        _store.submit( [accountID, role, nowMs]( IServiceStoreConnection& connection )
        { return AdminStoreLogic::seedRole( connection, accountID, role, nowMs.count() ); },
                       [this]( AdminServiceResult result, ServiceStoreCompletion completion )
        { completeSeed( result, completion ); } );
    }

    void AdminService::update( UnixMilliseconds nowMs )
    {
        (void)nowMs; // 요청마다 시각을 인자로 받으므로 틱 시각을 들고 있지 않는다
        pollForwardedCompletions();
        _calls.publishPendingCount();
    }

    void AdminService::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        (void)host;
        update( nowMs );
    }

    void AdminService::completeCall( uint64 callID, AdminServiceResult serviceResult, AdminReply& inoutReply )
    {
        const auto extraIt = _mapCallToExtra.find( callID );
        if ( extraIt == _mapCallToExtra.end() )
        {
            _calls.warnUnknownCall( callID );
            return;
        }
        const AdminCallExtra pending              = extraIt->second; // 알림 · 넘기기가 표를 건드려도 칸이 남게 복사한다
        const bool           bLookupWithDirectory = pending._method == AdminMethod::LookupAccount::kID && serviceResult == AdminResult::Ok && _settings._pDirectory != nullptr;
        if ( bLookupWithDirectory ) // 신원은 저장소 조회가 채웠다(이름으로 찾았으면 계정 id 도). 접속 여부만 이 프로세스로 본다
            inoutReply._bOnline = _settings._pDirectory->isAccountOnline( inoutReply._identity._accountID ) ? SW_TRUE : SW_FALSE;
        const bool bRevoke = serviceResult == AdminResult::Ok && inoutReply._bRevokeSessions == SW_TRUE && inoutReply._bReplayed == SW_FALSE;
        if ( bRevoke && _settings._pSessionControl != nullptr )
        {
            const string_view reasonCode = inoutReply._sanction._reasonCode.empty() ? string_view( "sanction.suspended" ) : string_view( inoutReply._sanction._reasonCode );
            _settings._pSessionControl->revokeAccountSessions( pending._targetID, reasonCode, pending._nowMs );
        }
        const bool bForwardToDirectory = AdminMethod::isDirectoryMethod( pending._method ) && serviceResult == AdminResult::Ok;
        if ( bForwardToDirectory )
        {
            if ( forwardDirectoryCall( callID, pending ) )
                return; // 서버 디렉터리 키트의 완료가 응답한다
            serviceResult = AdminServiceResult::fail( OnlineError::kUnavailable );
        }
        const bool bForwardToLiveOps = AdminMethod::isLiveEventMethod( pending._method ) && serviceResult == AdminResult::Ok;
        if ( bForwardToLiveOps && forwardLiveEventCall( callID, pending, serviceResult, inoutReply ) )
            return; // 라이브 운영 키트의 완료가 응답한다
        const bool bBalanceChanged = pending._method == AdminMethod::AdjustAsset::kID && serviceResult == AdminResult::Ok && inoutReply._bReplayed == SW_FALSE;
        if ( bBalanceChanged && _settings._pBalanceNotification != nullptr )
            _settings._pBalanceNotification->notifyBalancesChanged( pending._targetID, inoutReply._listBalance ); // 대상 계정이 접속해 있으면 지갑이 바로 맞는다
        const bool bSanctionChanged = pending._method == AdminMethod::SetSanction::kID && serviceResult == AdminResult::Ok && inoutReply._bReplayed == SW_FALSE;
        if ( bSanctionChanged && _settings._pBus != nullptr )
            (void)ServiceSanctionBus::kChangedTopic.publish( *_settings._pBus, ServiceSanctionChanged{ pending._targetID } ); // 실패는 발행이 로그로 남겼다. 채팅은 묵힘 시한 안에 다시 읽는다
        respondCall( callID, serviceResult, inoutReply );
    }

    void AdminService::respondCall( uint64 callID, const AdminServiceResult& serviceResult, const AdminReply& reply )
    {
        _mapCallToExtra.erase( callID );
        if ( LocalCallTable::isLocal( callID ) )
        {
            OnlineResult<AdminReply> local; // 원격 클라이언트가 받는 것과 같은 모양이다 — 공통 범주는 `_errorCode`
            local._errorCode = serviceResult._errorCode;
            if ( serviceResult.isFailed() == false )
                local._reply = reply;
            (void)_localCalls.finish( callID, local );
        }
        else if ( finishCall( callID, serviceResult, reply ) == false ) // 모르는 번호를 조용히 넘기지 않는다
            _calls.warnUnknownCall( callID );
    }

    bool AdminService::forwardDirectoryCall( uint64 callID, const AdminCallExtra& pending )
    {
        IServerDirectoryControl* const pControl = _settings._pDirectoryControl;
        if ( pControl == nullptr )
            return false;
        const AdminRequest& request = pending._request;
        switch ( pending._method )
        {
            case AdminMethod::SetMaintenance::kID:
            {
                ServerDirectoryMaintenanceChange change;
                change._listAllowedAccount = request._listAccountID;
                change._scope              = request._scope;
                change._messageKey         = request._messageKey;
                change._startMs            = request._startMs;
                change._endMs              = request._endMs;
                pControl->setMaintenance( change, pending._adminID, request._memo, pending._nowMs, callID );
                return true;
            }
            case AdminMethod::ClearMaintenance::kID:
            {
                pControl->clearMaintenance( request._scope, pending._adminID, request._memo, pending._nowMs, callID );
                return true;
            }
            case AdminMethod::PostAnnouncement::kID:
            {
                ServerDirectoryAnnouncementChange change;
                change._text           = request._body;
                change._announcementID = request._announcementID;
                change._startMs        = request._startMs;
                change._endMs          = request._endMs;
                change._priority       = request._priority;
                change._bLiteralText   = request._bLiteralText;
                pControl->postAnnouncement( change, pending._adminID, pending._nowMs, callID );
                return true;
            }
            case AdminMethod::RemoveAnnouncement::kID:
            {
                pControl->removeAnnouncement( request._announcementID, pending._adminID, pending._nowMs, callID );
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    bool AdminService::forwardLiveEventCall( uint64 callID, const AdminCallExtra& pending, AdminServiceResult& inoutServiceResult, AdminReply& inoutReply )
    {
        ILiveEventControl* const pControl = _settings._pLiveEventControl;
        if ( pControl == nullptr )
        {
            inoutServiceResult = AdminServiceResult::fail( OnlineError::kUnavailable );
            return false;
        }
        const AdminRequest& request = pending._request;
        if ( pending._method == AdminMethod::ListLiveEvents::kID )
        {
            vector<string> listJSON;
            pControl->collectEventJSON( listJSON );
            for ( string& definitionJSON : listJSON )
            {
                if ( static_cast<int32>( inoutReply._listLiveEvent.size() ) >= AdminProtocol::kMaxLiveEventCount )
                    break; // 응답 상한이다. 운영 화면은 이 수보다 많은 이벤트를 두지 않는다
                if ( definitionJSON.size() <= static_cast<size_t>( AdminProtocol::kMaxEventJSONSize ) )
                    inoutReply._listLiveEvent.push_back( AdminLiveEventView{ std::move( definitionJSON ) } );
            }
            return false;
        }
        if ( pending._method == AdminMethod::PutLiveEvent::kID )
            pControl->putEventJSON( request._eventJSON, pending._adminID, request._memo, pending._nowMs, callID );
        else
            pControl->removeEvent( request._eventID, pending._adminID, request._memo, pending._nowMs, callID );
        return true;
    }

    void AdminService::finishForwardedCall( uint64 callID, AdminServiceResult result )
    {
        AdminReply reply;
        reply._result = result._result;
        respondCall( callID, result, reply );
    }

    void AdminService::pollForwardedCompletions()
    {
        if ( _settings._pLiveEventControl != nullptr )
        {
            vector<LiveEventControlCompletion> listLiveCompletion;
            _settings._pLiveEventControl->takeControlCompletions( listLiveCompletion );
            for ( const LiveEventControlCompletion& completion : listLiveCompletion )
            {
                AdminServiceResult result = AdminServiceResult::fail( OnlineError::kUnavailable );
                if ( completion._result == LiveEventControlResult::Ok )
                    result = AdminResult::Ok;
                else if ( completion._result == LiveEventControlResult::Invalid )
                    result = AdminServiceResult::fail( OnlineError::kInvalidRequest );
                else if ( completion._result == LiveEventControlResult::NotFound )
                    result = AdminResult::NotFound;
                finishForwardedCall( completion._requestTag, result );
            }
        }
        if ( _settings._pDirectoryControl == nullptr )
            return;
        vector<ServerDirectoryControlCompletion> listCompletion;
        _settings._pDirectoryControl->takeControlCompletions( listCompletion );
        for ( const ServerDirectoryControlCompletion& completion : listCompletion )
        {
            AdminServiceResult result = AdminServiceResult::fail( OnlineError::kUnavailable );
            if ( completion._result == ServerDirectoryControlResult::Ok )
                result = AdminResult::Ok;
            else if ( completion._result == ServerDirectoryControlResult::Invalid )
                result = AdminServiceResult::fail( OnlineError::kInvalidRequest );
            else if ( completion._result == ServerDirectoryControlResult::NotFound )
                result = AdminResult::NotFound;
            finishForwardedCall( completion._requestTag, result );
        }
    }

    void AdminService::completeSeed( AdminServiceResult result, ServiceStoreCompletion completion )
    {
        if ( result != AdminResult::Ok && completion.isCancelled() == false ) // 종료 때문에 취소된 일은 오류가 아니다
            SW_LOG_ERROR_ID( "AdminService.AdminServiceCouldNotSeed", "AdminService could not seed the first administrator (result %#, error %#)", getEnumName( result._result ), static_cast<int32>( result._errorCode ) );
    }
} // namespace sw
