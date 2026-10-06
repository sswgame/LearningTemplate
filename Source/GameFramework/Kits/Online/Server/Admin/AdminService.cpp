#include "pch.h"

#include "GameFramework/Kits/Online/Server/Admin/AdminService.h"

#include "Core/Network/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/AccountSessionControl.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Online/Server/Admin/AdminStoreLogic.h"

namespace sw
{
    SW_LOG_CALLER( "AdminService" );

    namespace
    {
        struct AdminServiceInternal
        {
            static constexpr const utf8* kArrMethodName[] = { "lookup_account", "adjust_asset", "set_sanction", "send_mail",
                                                              "bulk_mail", "create_campaign", "list_audit", "set_role" };
            static constexpr const utf8* kArrResultName[] = { "ok", "not_signed_in", "forbidden", "invalid_request", "unknown_account",
                                                              "insufficient_funds", "cap_exceeded", "busy", "unavailable" };
            static_assert( SW_COUNT_OF( kArrMethodName ) == static_cast<size_t>( AdminMethod::kCount ), "method names must match AdminMethod" );
            static_assert( SW_COUNT_OF( kArrResultName ) == static_cast<size_t>( AdminResult::Count ), "result names must match AdminResult" );
            static constexpr int64 kShutdownWaitMs = 5000;
        };

        /** @brief GM 명령 하나의 저장 왕복입니다. */
        class AdminCommandWork final : public IServiceStoreWork
        {
        public:
            AdminCommandWork( AdminService* pService, const AdminCommand& command, uint64 callId )
                : _command{ command }
                , _reply{}
                , _pService{ pService }
                , _callId{ callId }
            {
            }

            void run( IServiceStoreConnection& connection ) override { (void)AdminStoreLogic::execute( connection, _command, _reply ); }
            void complete() override { _pService->completeCall( _callId, _reply ); }

        private:
            AdminCommand  _command;
            AdminReply    _reply;
            AdminService* _pService;
            uint64        _callId;
        };

        /** @brief 첫 관리자 넣기입니다. */
        class AdminSeedWork final : public IServiceStoreWork
        {
        public:
            AdminSeedWork( AdminService* pService, AccountId accountId, AdminRole role, int64 nowMs )
                : _pService{ pService }
                , _accountId{ accountId }
                , _nowMs{ nowMs }
                , _role{ role }
                , _result{ AdminResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = AdminStoreLogic::seedRole( connection, _accountId, _role, _nowMs ); }
            void complete() override { _pService->completeSeed( _result ); }

        private:
            AdminService* _pService;
            AccountId     _accountId;
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
        , _nextCallId{ 1 }
        , _nowMs{ 0 }
        , _pendingSeedCount{ 0 }
    {
    }

    AdminService::~AdminService() { SW_ASSERT( _listPendingCall.empty() ); }

    bool AdminService::initialize( IServiceStore* pStore, const AdminServiceSettings& settings )
    {
        if ( pStore == nullptr )
        {
            SW_LOG_ERROR( "AdminService needs a store" );
            return false;
        }
        _pStore   = pStore;
        _settings = settings;
        _metrics.initialize( settings._pMetricRegistry, "admin", AdminServiceInternal::kArrMethodName, AdminMethod::kCount, AdminServiceInternal::kArrResultName,
                             static_cast<int32>( AdminResult::Count ) );
        return true;
    }

    void AdminService::shutdown()
    {
        const Deadline deadline = Deadline::afterMilliseconds( AdminServiceInternal::kShutdownWaitMs );
        while ( _pStore != nullptr && ( _listPendingCall.empty() == false || _pendingSeedCount > 0 ) && deadline.isExpired() == false )
        {
            if ( _pStore->pollCompletions() == 0 )
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 1000000 );
        }
        if ( _listPendingCall.empty() == false )
        {
            SW_LOG_ERROR( "AdminService shut down with %# calls still pending", _listPendingCall.size() );
            _listPendingCall.clear();
        }
        _pStore = nullptr;
    }

    void AdminService::submitCall( AccountId adminId, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, int64 nowMs, ReplyDelegate onReply )
    {
        PendingCall pending;
        pending._onReply = onReply;
        startCall( adminId, method, request, idempotencyKey, nowMs, pending );
    }

    void AdminService::startCall( AccountId adminId, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, int64 nowMs, PendingCall pending )
    {
        pending._callId              = _nextCallId++;
        pending._receivedNanoseconds = MonotonicClock::nowNanoseconds();
        pending._nowMs               = nowMs != 0 ? nowMs : _nowMs;
        pending._targetId            = request._accountId;
        pending._methodIndex         = AdminMethod::toIndex( method );
        pending._method              = method;
        _listPendingCall.push_back( pending );
        const size_t pendingIndex = _listPendingCall.size() - 1;

        AdminCommand command;
        command._request = request;
        command._pPolicy = _settings._pPolicy;
        command._adminId = adminId;
        command._keyHigh = idempotencyKey._high;
        command._keyLow  = idempotencyKey._low;
        command._nowMs   = pending._nowMs;
        command._method  = method;

        AdminReply refused;
        refused._result = AdminResult::Ok;
        if ( _pStore == nullptr )
            refused._result = AdminResult::Unavailable;
        else if ( adminId == kInvalidAccountId )
            refused._result = AdminResult::NotSignedIn;
        else if ( pending._methodIndex < 0 )
            refused._result = AdminResult::Forbidden; // 모르는 명령 — 누구도 갖지 못한 등급
        const bool bByName = method == AdminMethod::kLookupAccount && request._displayName.empty() == false && request._accountId == kInvalidAccountId;
        if ( refused._result == AdminResult::Ok && bByName )
        {
            AccountIdentity identity;
            if ( _settings._pDirectory == nullptr || _settings._pDirectory->findIdentityByDisplayName( request._displayName, identity ) == false )
                refused._result = AdminResult::UnknownAccount; // 이 프로세스에 붙은 계정만 이름으로 찾는다
            else
                command._request._accountId = identity._accountId;
            _listPendingCall[pendingIndex]._targetId = command._request._accountId;
        }
        if ( refused._result != AdminResult::Ok )
        {
            finishCall( pendingIndex, refused );
            return;
        }
        _pStore->submit( make_unique<AdminCommandWork>( this, command, pending._callId ) );
    }

    void AdminService::seedRole( AccountId accountId, AdminRole role, int64 nowMs )
    {
        if ( _pStore == nullptr || accountId == kInvalidAccountId || role == AdminRole::None || role >= AdminRole::Count )
            return;
        ++_pendingSeedCount;
        _pStore->submit( make_unique<AdminSeedWork>( this, accountId, role, nowMs ) );
    }

    void AdminService::tick( int64 nowMs )
    {
        _nowMs = nowMs;
        _metrics.setPendingStoreWorkCount( static_cast<int32>( _listPendingCall.size() ) );
    }

    void AdminService::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        AdminRequest request;
        if ( AdminProtocol::readRequest( body, request ) == false )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        PendingCall pending;
        pending._pHost = &host;
        pending._token = context._token;
        startCall( context._accountId, context._method, request, context._idempotencyKey, context._nowMs, pending );
    }

    void AdminService::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        (void)host;
        tick( nowMs );
    }

    void AdminService::completeCall( uint64 callId, AdminReply& inoutReply )
    {
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingCall.size(); ++pendingIndex )
        {
            const PendingCall& pending = _listPendingCall[pendingIndex];
            if ( pending._callId != callId )
                continue;
            if ( pending._method == AdminMethod::kLookupAccount && inoutReply._result == AdminResult::Ok && _settings._pDirectory != nullptr )
            {
                AccountIdentity identity;
                if ( _settings._pDirectory->findIdentity( pending._targetId, identity ) )
                    inoutReply._identity = identity;
                inoutReply._identity._accountId = pending._targetId;
                inoutReply._bOnline             = _settings._pDirectory->isAccountOnline( pending._targetId ) ? SW_TRUE : SW_FALSE;
            }
            const bool bRevoke = inoutReply._result == AdminResult::Ok && inoutReply._bRevokeSessions == SW_TRUE && inoutReply._bReplayed == SW_FALSE;
            if ( bRevoke && _settings._pSessionControl != nullptr )
            {
                const string_view reasonCode = inoutReply._sanction._reasonCode.empty() ? string_view( "sanction.suspended" ) : string_view( inoutReply._sanction._reasonCode );
                _settings._pSessionControl->revokeAccountSessions( pending._targetId, reasonCode, pending._nowMs );
            }
            finishCall( pendingIndex, inoutReply );
            return;
        }
        SW_LOG_WARNING( "AdminService completed an unknown call %#", callId );
    }

    void AdminService::completeSeed( AdminResult result )
    {
        --_pendingSeedCount;
        if ( result != AdminResult::Ok )
            SW_LOG_ERROR( "AdminService could not seed the first administrator (%#)", toString( result ) );
    }

    void AdminService::finishCall( size_t pendingIndex, const AdminReply& reply )
    {
        const PendingCall pending = _listPendingCall[pendingIndex];
        _listPendingCall.erase( _listPendingCall.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
        if ( pending._pHost != nullptr )
        {
            BitWriter writer;
            AdminProtocol::writeReply( writer, reply );
            (void)pending._pHost->respondOk( pending._token, writer );
        }
        else if ( pending._onReply.isBound() )
        {
            pending._onReply( reply );
        }
        _metrics.countRequest( pending._methodIndex, static_cast<int32>( reply._result ) );
        MetricHistogram* pLatency = _metrics.findLatencyHistogram( pending._methodIndex );
        if ( pLatency != nullptr )
            pLatency->observe( static_cast<float64>( MonotonicClock::nowNanoseconds() - pending._receivedNanoseconds ) * 1e-9 );
    }
} // namespace sw
