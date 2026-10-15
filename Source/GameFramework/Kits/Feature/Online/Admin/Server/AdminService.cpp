#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"

#include "Core/Network/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/AccountSessionControl.h"
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
        , _nextCallID{ 1 }
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
        ServiceStoreUtil::drainUntilIdle( _pStore, [this]()
        { return _listPendingCall.empty() && _pendingSeedCount <= 0; } );
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
        pending._callID              = _nextCallID++;
        pending._receivedNanoseconds = MonotonicClock::nowNanoseconds();
        pending._nowMs               = nowMs != 0 ? nowMs : _nowMs;
        pending._targetID            = request._accountID;
        pending._methodIndex         = AdminMethod::toIndex( method );
        pending._method              = method;
        _listPendingCall.push_back( pending );
        const size_t pendingIndex = _listPendingCall.size() - 1;

        AdminCommand command;
        command._request = request;
        command._pPolicy = _settings._pPolicy;
        command._adminID = adminID;
        command._keyHigh = idempotencyKey._high;
        command._keyLow  = idempotencyKey._low;
        command._nowMs   = pending._nowMs;
        command._method  = method;

        AdminReply refused;
        refused._result = AdminResult::Ok;
        if ( _pStore == nullptr )
            refused._result = AdminResult::Unavailable;
        else if ( adminID == kInvalidAccountID )
            refused._result = AdminResult::NotSignedIn;
        else if ( pending._methodIndex < 0 )
            refused._result = AdminResult::Forbidden; // 모르는 명령이므로 누구도 가질 수 없는 등급이 필요하다
        const bool bByName = method == AdminMethod::kLookupAccount && request._displayName.empty() == false && request._accountID == kInvalidAccountID;
        if ( refused._result == AdminResult::Ok && bByName )
        {
            AccountIdentity identity;
            if ( _settings._pDirectory == nullptr || _settings._pDirectory->findIdentityByDisplayName( request._displayName, identity ) == false )
                refused._result = AdminResult::UnknownAccount; // 이 프로세스에 접속한 계정만 이름으로 찾는다
            else
                command._request._accountID = identity._accountID;
            _listPendingCall[pendingIndex]._targetID = command._request._accountID;
        }
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
            if ( bLookupWithDirectory )
            {
                AccountIdentity identity;
                if ( _settings._pDirectory->findIdentity( pending._targetID, identity ) )
                    inoutReply._identity = identity;
                inoutReply._identity._accountID = pending._targetID;
                inoutReply._bOnline             = _settings._pDirectory->isAccountOnline( pending._targetID ) ? SW_TRUE : SW_FALSE;
            }
            const bool bRevoke = inoutReply._result == AdminResult::Ok && inoutReply._bRevokeSessions == SW_TRUE && inoutReply._bReplayed == SW_FALSE;
            if ( bRevoke && _settings._pSessionControl != nullptr )
            {
                const string_view reasonCode = inoutReply._sanction._reasonCode.empty() ? string_view( "sanction.suspended" ) : string_view( inoutReply._sanction._reasonCode );
                _settings._pSessionControl->revokeAccountSessions( pending._targetID, reasonCode, pending._nowMs );
            }
            const bool bSanctionChanged = pending._method == AdminMethod::kSetSanction && inoutReply._result == AdminResult::Ok && inoutReply._bReplayed == SW_FALSE;
            if ( bSanctionChanged && _settings._pBus != nullptr )
            {
                BitWriter body;
                body.writeVarUint( pending._targetID );
                _settings._pBus->publish( ServiceSanctionBus::kChangedTopic, body.getBytes().data(), body.getByteCount() );
            }
            finishCall( pendingIndex, inoutReply );
            return;
        }
        SW_LOG_WARNING( "AdminService completed an unknown call %#", callID );
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
