#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Base/Online/Config/LiveEventControl.h"
#include "GameFramework/Base/Online/Directory/ServerDirectoryControl.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/AccountSessionControl.h"
#include "GameFramework/Base/Online/Ledger/IBalanceNotice.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceStoreUtil.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.h"

namespace sw
{
    SW_LOG_CALLER( "AdminService" );

    namespace
    {
        /** @brief GM 명령 하나의 저장소 왕복입니다. */
        class AdminCommandWork final : public IServiceStoreWork
        {
        public:
            AdminCommandWork( AdminService* pService, const AdminCommand& command, uint64 callID )
                : _command{ command }
                , _reply{}
                , _pService{ pService }
                , _callID{ callID }
            {
            }

            void run( IServiceStoreConnection& connection ) override { (void)AdminStoreLogic::execute( connection, _command, _reply ); }
            void complete() override { _pService->completeCall( _callID, _reply ); }

        private:
            AdminCommand  _command;
            AdminReply    _reply;
            AdminService* _pService;
            uint64        _callID;
        };

        /** @brief 첫 관리자 넣기입니다. */
        class AdminSeedWork final : public IServiceStoreWork
        {
        public:
            AdminSeedWork( AdminService* pService, AccountID accountID, AdminRole role, int64 nowMs )
                : _pService{ pService }
                , _accountID{ accountID }
                , _nowMs{ nowMs }
                , _role{ role }
                , _result{ AdminResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = AdminStoreLogic::seedRole( connection, _accountID, _role, _nowMs ); }
            void complete() override { _pService->completeSeed( _result ); }

        private:
            AdminService* _pService;
            AccountID     _accountID;
            int64         _nowMs;
            AdminRole     _role;
            AdminResult   _result;
        };
    } // namespace
} // namespace sw

namespace sw
{
    AdminService::AdminService()
        : _listPendingCall{}
        , _metrics{}
        , _settings{}
        , _pStore{ nullptr }
        , _callIDAllocator{}
        , _nowMs{ 0 }
        , _pendingSeedCount{ 0 }
    {
    }

    AdminService::~AdminService()
    {
        SW_ASSERT( _listPendingCall.empty() );
    }

    bool AdminService::initialize( IServiceStore* pStore, const AdminServiceConfig& settings )
    {
        if ( pStore == nullptr )
        {
            SW_LOG_ERROR( "AdminService needs a store" );
            return false;
        }
        _pStore   = pStore;
        _settings = settings;
        _metrics.initialize<AdminMethodName, AdminResult>( settings._pMetricRegistry, "admin" );
        return true;
    }

    void AdminService::shutdown()
    {
        // 넘긴 점검과 공지 명령은 서버 디렉터리 키트의 저장 완료 뒤에 끝나므로 회수할 때마다 그 완료도 회수한다
        ServiceStoreUtil::drainUntilIdle(
            _pStore, [this]()
        { return _listPendingCall.empty() && _pendingSeedCount <= 0; }, [this]()
        { pollForwardedCompletions(); } );
        if ( _listPendingCall.empty() == false )
        {
            SW_LOG_ERROR( "AdminService shut down with %# calls still pending", _listPendingCall.size() );
            _listPendingCall.clear();
        }
        _pStore = nullptr;
    }

    void AdminService::submitCall( AccountID adminID, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, int64 nowMs, ReplyDelegate onReply )
    {
        PendingCall pending;
        pending._onReply = onReply;
        startCall( adminID, method, request, idempotencyKey, nowMs, pending );
    }

    void AdminService::startCall( AccountID adminID, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, int64 nowMs, PendingCall pending )
    {
        pending._callID              = _callIDAllocator.allocate();
        pending._receivedNanoseconds = MonotonicClock::nowNanoseconds();
        pending._nowMs               = nowMs != 0 ? nowMs : _nowMs;
        pending._targetID            = request._accountID;
        pending._adminID             = adminID;
        pending._methodIndex         = AdminMethod::toIndex( method );
        pending._method              = method;
        if ( AdminMethod::isForwardedMethod( method ) )
            pending._request = request;
        _listPendingCall.push_back( pending );
        const size_t pendingIndex = _listPendingCall.size() - 1;

        AdminCommand command;
        command._request    = request;
        command._pPolicy    = _settings._pPolicy;
        command._pNameIndex = _settings._pNameIndex;
        command._adminID    = adminID;
        command._keyHigh    = idempotencyKey._high;
        command._keyLow     = idempotencyKey._low;
        command._nowMs      = pending._nowMs;
        command._method     = method;

        AdminReply refused;
        refused._result = AdminResult::Ok;
        if ( _pStore == nullptr )
            refused._result = AdminResult::Unavailable;
        else if ( adminID == kInvalidAccountID )
            refused._result = AdminResult::NotSignedIn;
        else if ( pending._methodIndex < 0 )
            refused._result = AdminResult::Forbidden; // 모르는 명령이므로 누구도 가질 수 없는 등급이 필요하다
        if ( refused._result != AdminResult::Ok )
        {
            finishCall( pendingIndex, refused );
            return;
        }
        _pStore->submit( make_unique<AdminCommandWork>( this, command, pending._callID ) );
    }

    void AdminService::seedRole( AccountID accountID, AdminRole role, int64 nowMs )
    {
        const bool bInvalidSeed = _pStore == nullptr || accountID == kInvalidAccountID || role == AdminRole::None || role >= AdminRole::Count;
        if ( bInvalidSeed )
            return;
        ++_pendingSeedCount;
        _pStore->submit( make_unique<AdminSeedWork>( this, accountID, role, nowMs ) );
    }

    void AdminService::update( int64 nowMs )
    {
        _nowMs = nowMs;
        pollForwardedCompletions();
        _metrics.setPendingStoreWorkCount( static_cast<int32>( _listPendingCall.size() ) );
    }

    void AdminService::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        AdminRequest request;
        if ( AdminProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        PendingCall pending;
        pending._pHost = &host;
        pending._token = context._token;
        startCall( context._accountID, context._method, request, context._idempotencyKey, context._nowMs, pending );
    }

    void AdminService::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        (void)host;
        update( nowMs );
    }

    void AdminService::completeCall( uint64 callID, AdminReply& inoutReply )
    {
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingCall.size(); ++pendingIndex )
        {
            const PendingCall& pending = _listPendingCall[pendingIndex];
            if ( pending._callID != callID )
                continue;
            const bool bLookupWithDirectory = pending._method == AdminMethod::kLookupAccount && inoutReply._result == AdminResult::Ok && _settings._pDirectory != nullptr;
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
                if ( forwardDirectoryCall( pending ) )
                    return; // 서버 디렉터리 키트의 완료가 응답한다
                inoutReply._result = AdminResult::Unavailable;
            }
            const bool bForwardToLiveOps = AdminMethod::isLiveEventMethod( pending._method ) && inoutReply._result == AdminResult::Ok;
            if ( bForwardToLiveOps && forwardLiveEventCall( pending, inoutReply ) )
                return; // 라이브 운영 키트의 완료가 응답한다
            const bool bBalanceChanged = pending._method == AdminMethod::kAdjustAsset && inoutReply._result == AdminResult::Ok && inoutReply._bReplayed == SW_FALSE;
            if ( bBalanceChanged && _settings._pBalanceNotice != nullptr )
                _settings._pBalanceNotice->notifyBalancesChanged( pending._targetID, inoutReply._listBalance ); // 대상 계정이 접속해 있으면 지갑이 바로 맞는다
            const bool bSanctionChanged = pending._method == AdminMethod::kSetSanction && inoutReply._result == AdminResult::Ok && inoutReply._bReplayed == SW_FALSE;
            if ( bSanctionChanged && _settings._pBus != nullptr )
                (void)ServiceSanctionBus::kChangedTopic.publish( *_settings._pBus, ServiceSanctionChanged{ pending._targetID } ); // 실패는 발행이 로그로 남겼다. 채팅은 묵힘 시한 안에 다시 읽는다
            finishCall( pendingIndex, inoutReply );
            return;
        }
        SW_LOG_WARNING( "AdminService completed an unknown call %#", callID );
    }

    bool AdminService::forwardDirectoryCall( const PendingCall& pending )
    {
        IServerDirectoryControl* const pControl = _settings._pDirectoryControl;
        if ( pControl == nullptr )
            return false;
        const AdminRequest& request = pending._request;
        switch ( pending._method )
        {
            case AdminMethod::kSetMaintenance:
            {
                ServerDirectoryMaintenanceChange change;
                change._listAllowedAccount = request._listAccountID;
                change._scope              = request._scope;
                change._messageKey         = request._messageKey;
                change._startMs            = request._startMs;
                change._endMs              = request._endMs;
                pControl->setMaintenance( change, pending._adminID, request._memo, pending._nowMs, pending._callID );
                return true;
            }
            case AdminMethod::kClearMaintenance:
            {
                pControl->clearMaintenance( request._scope, pending._adminID, request._memo, pending._nowMs, pending._callID );
                return true;
            }
            case AdminMethod::kPostNotice:
            {
                ServerDirectoryNoticeChange change;
                change._text         = request._body;
                change._noticeID     = request._noticeID;
                change._startMs      = request._startMs;
                change._endMs        = request._endMs;
                change._priority     = request._priority;
                change._bLiteralText = request._bLiteralText;
                pControl->postNotice( change, pending._adminID, pending._nowMs, pending._callID );
                return true;
            }
            case AdminMethod::kRemoveNotice:
            {
                pControl->removeNotice( request._noticeID, pending._adminID, pending._nowMs, pending._callID );
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    bool AdminService::forwardLiveEventCall( const PendingCall& pending, AdminReply& inoutReply )
    {
        ILiveEventControl* const pControl = _settings._pLiveEventControl;
        if ( pControl == nullptr )
        {
            inoutReply._result = AdminResult::Unavailable;
            return false;
        }
        const AdminRequest& request = pending._request;
        if ( pending._method == AdminMethod::kListLiveEvents )
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
        if ( pending._method == AdminMethod::kPutLiveEvent )
            pControl->putEventJSON( request._eventJSON, pending._adminID, request._memo, pending._nowMs, pending._callID );
        else
            pControl->removeEvent( request._eventID, pending._adminID, request._memo, pending._nowMs, pending._callID );
        return true;
    }

    void AdminService::finishForwardedCall( uint64 callID, AdminResult result )
    {
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingCall.size(); ++pendingIndex )
        {
            if ( _listPendingCall[pendingIndex]._callID != callID )
                continue;
            AdminReply reply;
            reply._result = result;
            finishCall( pendingIndex, reply );
            return;
        }
        SW_LOG_WARNING( "AdminService got a forwarded completion for an unknown call %#", callID );
    }

    void AdminService::pollForwardedCompletions()
    {
        if ( _settings._pLiveEventControl != nullptr )
        {
            vector<LiveEventControlCompletion> listLiveCompletion;
            _settings._pLiveEventControl->drainControlCompletions( listLiveCompletion );
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
        _settings._pDirectoryControl->drainControlCompletions( listCompletion );
        for ( const ServerDirectoryControlCompletion& completion : listCompletion )
        {
            AdminReply reply;
            switch ( completion._result )
            {
                case ServerDirectoryControlResult::Ok:
                {
                    reply._result = AdminResult::Ok;
                    break;
                }
                case ServerDirectoryControlResult::Invalid:
                {
                    reply._result = AdminResult::InvalidRequest;
                    break;
                }
                case ServerDirectoryControlResult::NotFound:
                {
                    reply._result = AdminResult::NotFound;
                    break;
                }
                case ServerDirectoryControlResult::Unavailable:
                {
                    reply._result = AdminResult::Unavailable;
                    break;
                }
            }
            bool bFound = false;
            for ( size_t pendingIndex = 0; pendingIndex < _listPendingCall.size(); ++pendingIndex )
            {
                if ( _listPendingCall[pendingIndex]._callID != completion._requestTag )
                    continue;
                finishCall( pendingIndex, reply );
                bFound = true;
                break;
            }
            if ( bFound == false )
                SW_LOG_WARNING( "AdminService got a server directory completion for an unknown call %#", completion._requestTag );
        }
    }

    void AdminService::completeSeed( AdminResult result )
    {
        --_pendingSeedCount;
        if ( result != AdminResult::Ok )
            SW_LOG_ERROR( "AdminService could not seed the first administrator (%#)", getEnumName( result ) );
    }

    void AdminService::finishCall( size_t pendingIndex, const AdminReply& reply )
    {
        const PendingCall pending = _listPendingCall[pendingIndex];
        _listPendingCall.erase( _listPendingCall.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
        if ( pending._pHost != nullptr )
            (void)pending._pHost->respondMessage<AdminProtocol::MessageTypes>( pending._token, reply );
        else if ( pending._onReply.isBound() )
            pending._onReply( reply );
        _metrics.countRequest( pending._methodIndex, static_cast<int32>( reply._result ) );
        MetricHistogram* pLatency = _metrics.findLatencyHistogram( pending._methodIndex );
        if ( pLatency != nullptr )
            pLatency->observe( static_cast<float64>( MonotonicClock::nowNanoseconds() - pending._receivedNanoseconds ) * 1e-9 );
    }
} // namespace sw
