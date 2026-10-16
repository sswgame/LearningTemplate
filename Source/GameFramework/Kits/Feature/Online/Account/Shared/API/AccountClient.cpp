#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Shared/API/AccountClient.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetHostSecurity.h"

#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountProtocol.h"

namespace sw
{
    namespace
    {
        struct AccountClientInternal
        {
            /** @brief 공통 오류 코드를 의미가 가까운 업무 결과로 바꿉니다. */
            static LoginResult toResult( uint16 errorCode )
            {
                static constexpr OnlineErrorResult<LoginResult> kArrErrorResult[] = {
                    {    OnlineError::kRateLimited,    LoginResult::RateLimited},
                    { OnlineError::kUpdateRequired, LoginResult::UpdateRequired},
                    {OnlineError::kVersionMismatch, LoginResult::UpdateRequired}, // 스키마 해시가 다르다. 다시 접속하지 않으므로 "저장소 오류" 로 두면 재시도만 부른다
                    {OnlineError::kUnauthenticated,   LoginResult::InvalidToken},
                    { OnlineError::kInvalidRequest, LoginResult::InvalidRequest},
                };
                return OnlineError::toResult( errorCode, kArrErrorResult, LoginResult::StoreUnavailable );
            }

            static bool isGrantOperation( AccountClientOperation operation )
            {
                return operation == AccountClientOperation::Login || operation == AccountClientOperation::GuestLogin ||
                       operation == AccountClientOperation::PlatformLogin || operation == AccountClientOperation::Resume;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AccountClient::AccountClient()
        : _callTable{}
        , _listReply{}
        , _listEvent{}
        , _listDeferred{}
        , _clientInfo{}
        , _identity{}
        , _token{}
        , _pClient{ nullptr }
        , _nextRequestID{ 1 }
        , _bLoggedIn{ SW_FALSE }
        , _bResuming{ SW_FALSE }
    {
    }

    void AccountClient::initialize( OnlineServiceClient* pClient, const AccountClientInfo& clientInfo )
    {
        _pClient    = pClient;
        _clientInfo = clientInfo;
    }

    uint64 AccountClient::registerAccount( string_view loginName, string_view password )
    {
        BitWriter body;
        AccountWire::writeCredential( body, loginName, password );
        return send( AccountClientOperation::Register, AccountMethod::kRegister, body, false );
    }

    uint64 AccountClient::login( string_view loginName, string_view password )
    {
        BitWriter body;
        AccountWire::writeCredential( body, loginName, password );
        AccountWire::writeClientInfo( body, _clientInfo );
        return send( AccountClientOperation::Login, AccountMethod::kLogin, body, false );
    }

    uint64 AccountClient::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize] )
    {
        BitWriter body;
        body.writeBytes( arrDeviceSecret, LoginConstant::kDeviceSecretSize );
        AccountWire::writeClientInfo( body, _clientInfo );
        return send( AccountClientOperation::GuestLogin, AccountMethod::kGuestLogin, body, false );
    }

    uint64 AccountClient::platformLogin( string_view provider, const vector<uint8>& ticketBytes )
    {
        BitWriter body;
        AccountWire::writeText( body, provider );
        AccountWire::writeBlob( body, ticketBytes );
        AccountWire::writeClientInfo( body, _clientInfo );
        return send( AccountClientOperation::PlatformLogin, AccountMethod::kPlatformLogin, body, false );
    }

    uint64 AccountClient::resume()
    {
        if ( _token.isEmpty() )
        {
            AccountClientReply& reply = _listReply.emplace_back();
            reply._requestID          = _nextRequestID++;
            reply._operation          = AccountClientOperation::Resume;
            reply._result             = LoginResult::InvalidToken;
            return reply._requestID;
        }
        BitWriter body;
        AccountWire::writeToken( body, _token );
        AccountWire::writeClientInfo( body, _clientInfo );
        _bResuming = SW_TRUE;
        return send( AccountClientOperation::Resume, AccountMethod::kResume, body, false );
    }

    uint64 AccountClient::logout()
    {
        return send( AccountClientOperation::Logout, AccountMethod::kLogout, BitWriter{}, true );
    }

    uint64 AccountClient::linkCredential( string_view loginName, string_view password )
    {
        BitWriter body;
        AccountWire::writeCredential( body, loginName, password );
        return send( AccountClientOperation::LinkCredential, AccountMethod::kLinkCredential, body, true );
    }

    uint64 AccountClient::linkPlatform( string_view provider, const vector<uint8>& ticketBytes )
    {
        BitWriter body;
        AccountWire::writeText( body, provider );
        AccountWire::writeBlob( body, ticketBytes );
        return send( AccountClientOperation::LinkPlatform, AccountMethod::kLinkPlatform, body, true );
    }

    uint64 AccountClient::issueGameTicket( string_view serverID )
    {
        BitWriter body;
        AccountWire::writeText( body, serverID );
        return send( AccountClientOperation::IssueGameTicket, AccountMethod::kIssueGameTicket, body, true );
    }

    uint64 AccountClient::unlinkPlatform( string_view provider )
    {
        BitWriter body;
        AccountWire::writeText( body, provider );
        return send( AccountClientOperation::UnlinkPlatform, AccountMethod::kUnlinkPlatform, body, true );
    }

    uint64 AccountClient::listLinks()
    {
        return send( AccountClientOperation::ListLinks, AccountMethod::kListLinks, BitWriter{}, true );
    }

    uint64 AccountClient::requestDeletion()
    {
        return send( AccountClientOperation::RequestDeletion, AccountMethod::kRequestDeletion, BitWriter{}, true );
    }

    uint64 AccountClient::cancelDeletion()
    {
        return send( AccountClientOperation::CancelDeletion, AccountMethod::kCancelDeletion, BitWriter{}, true );
    }

    int32 AccountClient::pollReplies( vector<AccountClientReply>& outListReply )
    {
        const int32 count = static_cast<int32>( _listReply.size() );
        for ( AccountClientReply& reply : _listReply )
        {
            outListReply.push_back( std::move( reply ) );
        }
        _listReply.clear();
        return count;
    }

    int32 AccountClient::pollEvents( vector<AccountClientEvent>& outListEvent )
    {
        const int32 count = static_cast<int32>( _listEvent.size() );
        for ( AccountClientEvent& event : _listEvent )
        {
            outListEvent.push_back( std::move( event ) );
        }
        _listEvent.clear();
        return count;
    }

    void AccountClient::makeConnectCredentials( const NetGameTicket& ticket, NetConnectCredentials& outCredentials )
    {
        static_assert( NetGameTicket::kTokenSize <= NetSecurityConstant::kMaxTokenSize );
        static_assert( NetGameTicket::kSecretSize == sizeof( outCredentials._secret._arrByte ) );
        Memory::copy( outCredentials._arrToken, ticket._arrToken, NetGameTicket::kTokenSize );
        outCredentials._tokenSize = NetGameTicket::kTokenSize;
        Memory::copy( outCredentials._secret._arrByte, ticket._arrSecret, NetGameTicket::kSecretSize );
    }

    const OnlineProtocolEntry& AccountClient::getProtocol() const
    {
        return AccountProtocol::kOnlineEntry;
    }

    void AccountClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != AccountMethod::kPushRevoked )
            return;
        AccountClientEvent event;
        if ( AccountWire::readRevokedPush( body, event._reason, event._reasonCode ) == false )
            return;
        _bLoggedIn = SW_FALSE;
        _token     = LoginSessionToken{}; // 밀려난 세션은 돌아올 수 없으므로 다시 로그인해야 한다
        _listEvent.push_back( std::move( event ) );
    }

    void AccountClient::onClientReady( OnlineServiceClient& client )
    {
        _bLoggedIn = SW_FALSE; // 새 연결이므로 서버는 아직 주체를 모른다
        if ( _token.isEmpty() == false && _bResuming == SW_FALSE )
        {
            client.setRequestGate( OnlineMethodRange::kAccount, true ); // 다른 키트의 요청은 재접속 응답 뒤에 보낸다
            BitWriter body;
            AccountWire::writeToken( body, _token );
            AccountWire::writeClientInfo( body, _clientInfo );
            _bResuming = SW_TRUE;
            (void)send( AccountClientOperation::Resume, AccountMethod::kResume, body, false, true );
        }
    }

    void AccountClient::onClientDisconnected( OnlineServiceClient& client )
    {
        (void)client;
        _bLoggedIn = SW_FALSE;
    }

    uint64 AccountClient::send( AccountClientOperation operation, uint16 method, const BitWriter& body, bool bNeedsSession, bool bAutomatic )
    {
        const uint64        requestID = _nextRequestID++;
        const vector<uint8> bodyBytes( body.getBytes().begin(), body.getBytes().begin() + body.getByteCount() );
        if ( bNeedsSession && _bResuming == SW_TRUE )
        {
            _listDeferred.push_back( DeferredCall{ bodyBytes, requestID, method, operation } ); // 재접속 응답 뒤에 보내도록 모아 둔다
            return requestID;
        }
        transmit( operation, method, bodyBytes, requestID, bAutomatic );
        return requestID;
    }

    void AccountClient::transmit( AccountClientOperation operation, uint16 method, const vector<uint8>& bodyBytes, uint64 requestID, bool bAutomatic )
    {
        BitWriter body;
        if ( bodyBytes.empty() == false )
            body.writeBytes( bodyBytes.data(), static_cast<int32>( bodyBytes.size() ) );
        NetRequestOptions options;
        const PendingCall pendingCall{ requestID, operation, static_cast<uint8>( bAutomatic ? SW_TRUE : SW_FALSE ) };
        (void)_callTable.send( *_pClient, method, body, options, OnlineResponseDelegate::create<&AccountClient::onResponse>( this ), pendingCall ); // 응답은 pendingCall._requestID 로 알린다
    }

    void AccountClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        if ( _callTable.take( response._requestID, call ) == false )
            return;

        AccountClientReply reply;
        reply._requestID  = call._requestID;
        reply._operation  = call._operation;
        reply._bAutomatic = call._bAutomatic;
        reply._errorCode  = response._errorCode;
        if ( response.isOk() == false )
        {
            reply._result = AccountClientInternal::toResult( response._errorCode );
            BitReader detail( response._pBody, response._bodySize );
            if ( response._errorCode == OnlineError::kUpdateRequired )
                (void)AccountWire::readText( detail, 512, reply._grant._storeURL ); // 상세가 깨졌으면 상점 주소만 빈다. 결과 코드는 위에서 이미 정했다
            else if ( response._errorCode == OnlineError::kRateLimited )
                reply._grant._retryAfterMs = static_cast<int64>( detail.readVarUint() );
        }
        else
        {
            BitReader body( response._pBody, response._bodySize );
            bool      bRead = false;
            if ( call._operation == AccountClientOperation::IssueGameTicket )
                bRead = AccountWire::readTicketReply( body, reply._result, reply._ticket );
            else if ( call._operation == AccountClientOperation::ListLinks )
                bRead = AccountWire::readLinkReply( body, reply._result, reply._linkSummary );
            else
                bRead = AccountWire::readGrantReply( body, reply._result, reply._grant );
            if ( bRead == false )
                reply._result = LoginResult::InvalidRequest;
        }

        const bool bOk = reply._result == LoginResult::Ok;
        if ( bOk && AccountClientInternal::isGrantOperation( call._operation ) )
        {
            _bLoggedIn = SW_TRUE;
            _token     = reply._grant._token;
            _identity  = reply._grant._identity;
        }
        if ( bOk && ( call._operation == AccountClientOperation::LinkCredential || call._operation == AccountClientOperation::LinkPlatform ) )
            _identity = reply._grant._identity;
        if ( bOk && ( call._operation == AccountClientOperation::Logout || call._operation == AccountClientOperation::RequestDeletion ) )
        {
            _bLoggedIn = SW_FALSE;
            _token     = LoginSessionToken{};
        }
        if ( call._operation == AccountClientOperation::Resume )
        {
            const bool bTokenDead = reply._result == LoginResult::InvalidToken || reply._result == LoginResult::Expired || reply._result == LoginResult::Revoked;
            if ( bTokenDead )
                _token = LoginSessionToken{};
            _bResuming = SW_FALSE;
            _listReply.push_back( std::move( reply ) );
            finishDeferred( bOk ? LoginResult::Ok : LoginResult::InvalidToken );
            if ( _pClient != nullptr && _pClient->isRequestGateClosed() )
                _pClient->setRequestGate( OnlineMethodRange::kAccount, false ); // 다른 키트가 모아 둔 요청을 보낸다. 실패해도 그쪽이 kUnauthenticated 로 알 수 있다
            return;
        }
        _listReply.push_back( std::move( reply ) );
    }

    void AccountClient::finishDeferred( LoginResult failure )
    {
        vector<DeferredCall> listDeferred;
        listDeferred.swap( _listDeferred );
        for ( const DeferredCall& deferred : listDeferred )
        {
            if ( failure == LoginResult::Ok )
            {
                transmit( deferred._operation, deferred._method, deferred._bodyBytes, deferred._requestID, false );
                continue;
            }
            AccountClientReply& reply = _listReply.emplace_back();
            reply._requestID          = deferred._requestID;
            reply._operation          = deferred._operation;
            reply._result             = failure;
        }
    }
} // namespace sw
