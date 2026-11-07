#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Config/LiveEventControl.h"
#include "GameFramework/Runtime/Online/Directory/ServerDirectoryControl.h"
#include "GameFramework/Runtime/Online/Identity/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/IBalanceChangedListener.h"
#include "GameFramework/Runtime/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceStoreUtil.h"

namespace sw
{
    namespace
    {
        /** @brief GM 명령 하나의 저장소 왕복입니다. */
        class AdminCommandJob final : public IServiceStoreJob
        {
        public:
            AdminCommandJob( AdminService* pService, const AdminCommand& command, uint64 callID )
                : _command{ command }
                , _reply{}
                , _pService{ pService }
                , _callID{ callID }
            {
            }

            void run( IServiceStoreConnection& connection ) override { (void)AdminStoreLogic::execute( connection, _command, _reply ); } // 결과는 _reply 에 담겨 complete 가 돌려준다
            void complete() override { _pService->completeCall( _callID, _reply ); }

        private:
            AdminCommand  _command;
            AdminReply    _reply;
            AdminService* _pService;
            uint64        _callID;
        };

        /** @brief 첫 관리자 넣기입니다. */
        class AdminSeedJob final : public IServiceStoreJob
        {
        public:
            AdminSeedJob( AdminService* pService, AccountID accountID, AdminRole role, UnixMilliseconds nowMs )
                : _pService{ pService }
                , _accountID{ accountID }
                , _nowMs{ nowMs }
                , _role{ role }
                , _result{ AdminResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = AdminStoreLogic::seedRole( connection, _accountID, _role, _nowMs.count() ); }
            void complete() override { _pService->completeSeed( _result, getCompletion() ); }

        private:
            AdminService*    _pService;
            AccountID        _accountID;
            UnixMilliseconds _nowMs;
            AdminRole        _role;
            AdminResult      _result;
        };
    } // namespace
} // namespace sw

namespace sw
{
    AdminService::AdminService()
        : _localCalls{}
        , _mapCallToExtra{}
        , _settings{}
        , _pStore{ nullptr }
        , _pendingSeedCount{ 0 }
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
        _pStore   = pStore;
        _settings = settings;
        initializeCalls( settings._pMetricRegistry, "admin" );
        setReady( true );
        return true;
    }

    void AdminService::shutdown()
    {
        // 넘긴 점검과 공지 명령은 서버 디렉터리 키트의 저장 완료 뒤에 끝나므로 회수할 때마다 그 완료도 회수한다
        ServiceStoreUtil::flushUntilIdle(
            _pStore, [this]()
        { return _calls.isEmpty() && _localCalls.isEmpty() && _pendingSeedCount <= 0; }, [this]()
        { pollForwardedCompletions(); } );
        _calls.abandonAll();
        _localCalls.clear();
        _mapCallToExtra.clear();
        setReady( false );
        _pStore = nullptr;
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

        AdminReply refused;
        refused._result = AdminResult::Ok;
        if ( _pStore == nullptr )
            refused._result = AdminResult::Unavailable;
        else if ( adminID == kInvalidAccountID )
            refused._result = AdminResult::NotSignedIn;
        else if ( methodIndex < 0 )
            refused._result = AdminResult::Forbidden; // 모르는 명령이므로 누구도 가질 수 없는 등급이 필요하다
        if ( refused._result != AdminResult::Ok )
        {
            finishCall( callID, refused );
            return;
        }
        _pStore->submit( make_unique<AdminCommandJob>( this, command, callID ) );
    }

    void AdminService::seedRole( AccountID accountID, AdminRole role, UnixMilliseconds nowMs )
    {
        const bool bInvalidSeed = _pStore == nullptr || accountID == kInvalidAccountID || role == AdminRole::None || role >= AdminRole::Count;
        if ( bInvalidSeed )
            return;
        ++_pendingSeedCount;
        _pStore->submit( make_unique<AdminSeedJob>( this, accountID, role, nowMs ) );
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

    void AdminService::completeCall( uint64 callID, AdminReply& inoutReply )
    {
        const auto extraIt = _mapCallToExtra.find( callID );
        if ( extraIt == _mapCallToExtra.end() )
        {
            _calls.warnUnknownCall( callID );
            return;
        }
        const AdminCallExtra pending              = extraIt->second; // 알림 · 넘기기가 표를 건드려도 칸이 남게 복사한다
        const bool           bLookupWithDirectory = pending._method == AdminMethod::LookupAccount::kID && inoutReply._result == AdminResult::Ok && _settings._pDirectory != nullptr;
        if ( bLookupWithDirectory ) // 신원은 저장소 조회가 채웠다(이름으로 찾았으면 계정 id 도). 접속 여부만 이 프로세스로 본다
            inoutReply._bOnline = _settings._pDirectory->isAccountOnline( inoutReply._identity._accountID ) ? SW_TRUE : SW_FALSE;
        const bool bRevoke = inoutReply._result == AdminResult::Ok && inoutReply._bRevokeSessions == SW_TRUE && inoutReply._bReplayed == SW_FALSE;
        if ( bRevoke && _settings._pSessionControl != nullptr )
        {
            const string_view reasonCode = inoutReply._sanction._reasonCode.empty() ? string_view( "sanction.suspended" ) : string_view( inoutReply._sanction._reasonCode );
            _settings._pSessionControl->revokeAccountSessions( pending._targetID, reasonCode, pending._nowMs );
        }
        const bool bForwardToDirectory = AdminMethod::isDirectoryMethod( pending._method ) && inoutReply._result == AdminResult::Ok;
        if ( bForwardToDirectory )
        {
            if ( forwardDirectoryCall( callID, pending ) )
                return; // 서버 디렉터리 키트의 완료가 응답한다
            inoutReply._result = AdminResult::Unavailable;
        }
        const bool bForwardToLiveOps = AdminMethod::isLiveEventMethod( pending._method ) && inoutReply._result == AdminResult::Ok;
        if ( bForwardToLiveOps && forwardLiveEventCall( callID, pending, inoutReply ) )
            return; // 라이브 운영 키트의 완료가 응답한다
        const bool bBalanceChanged = pending._method == AdminMethod::AdjustAsset::kID && inoutReply._result == AdminResult::Ok && inoutReply._bReplayed == SW_FALSE;
        if ( bBalanceChanged && _settings._pBalanceNotice != nullptr )
            _settings._pBalanceNotice->notifyBalancesChanged( pending._targetID, inoutReply._listBalance ); // 대상 계정이 접속해 있으면 지갑이 바로 맞는다
        const bool bSanctionChanged = pending._method == AdminMethod::SetSanction::kID && inoutReply._result == AdminResult::Ok && inoutReply._bReplayed == SW_FALSE;
        if ( bSanctionChanged && _settings._pBus != nullptr )
            (void)ServiceSanctionBus::kChangedTopic.publish( *_settings._pBus, ServiceSanctionChanged{ pending._targetID } ); // 실패는 발행이 로그로 남겼다. 채팅은 묵힘 시한 안에 다시 읽는다
        finishCall( callID, inoutReply );
    }

    void AdminService::finishCall( uint64 callID, const AdminReply& reply )
    {
        _mapCallToExtra.erase( callID );
        if ( LocalCallTable::isLocal( callID ) )
            (void)_localCalls.finish( callID, reply );
        else if ( OnlineProtocolService::completeCall( callID, reply ) == false ) // 모르는 번호를 조용히 넘기지 않는다
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
            case AdminMethod::PostNotice::kID:
            {
                ServerDirectoryNoticeChange change;
                change._text         = request._body;
                change._noticeID     = request._noticeID;
                change._startMs      = request._startMs;
                change._endMs        = request._endMs;
                change._priority     = request._priority;
                change._bLiteralText = request._bLiteralText;
                pControl->postNotice( change, pending._adminID, pending._nowMs, callID );
                return true;
            }
            case AdminMethod::RemoveNotice::kID:
            {
                pControl->removeNotice( request._noticeID, pending._adminID, pending._nowMs, callID );
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    bool AdminService::forwardLiveEventCall( uint64 callID, const AdminCallExtra& pending, AdminReply& inoutReply )
    {
        ILiveEventControl* const pControl = _settings._pLiveEventControl;
        if ( pControl == nullptr )
        {
            inoutReply._result = AdminResult::Unavailable;
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

    void AdminService::finishForwardedCall( uint64 callID, AdminResult result )
    {
        AdminReply reply;
        reply._result = result;
        finishCall( callID, reply );
    }

    void AdminService::pollForwardedCompletions()
    {
        if ( _settings._pLiveEventControl != nullptr )
        {
            vector<LiveEventControlCompletion> listLiveCompletion;
            _settings._pLiveEventControl->takeControlCompletions( listLiveCompletion );
            for ( const LiveEventControlCompletion& completion : listLiveCompletion )
            {
                AdminResult result = AdminResult::Unavailable;
                if ( completion._result == LiveEventControlResult::Ok )
                    result = AdminResult::Ok;
                else if ( completion._result == LiveEventControlResult::Invalid )
                    result = AdminResult::InvalidRequest;
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
            AdminResult result = AdminResult::Unavailable;
            if ( completion._result == ServerDirectoryControlResult::Ok )
                result = AdminResult::Ok;
            else if ( completion._result == ServerDirectoryControlResult::Invalid )
                result = AdminResult::InvalidRequest;
            else if ( completion._result == ServerDirectoryControlResult::NotFound )
                result = AdminResult::NotFound;
            finishForwardedCall( completion._requestTag, result );
        }
    }

    void AdminService::completeSeed( AdminResult result, ServiceStoreCompletion completion )
    {
        --_pendingSeedCount;
        if ( result != AdminResult::Ok && completion.isCancelled() == false ) // 종료 때문에 취소된 일은 오류가 아니다
            SW_LOG_ERROR_ID( "AdminService.AdminServiceCouldNotSeed", "AdminService could not seed the first administrator (%#)", getEnumName( result ) );
    }
} // namespace sw
