#include "pch.h"

#include "GameFramework/Kits/Online/Account/AccountClient.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetHostSecurity.h"

#include "GameFramework/Kits/Online/Account/AccountProtocol.h"

namespace sw
{
    namespace
    {
        struct AccountClientInternal
        {
            /** @brief 공통 오류 코드를 가까운 업무 결과로 읽습니다. */
            static LoginResult toResult( uint16 errorCode )
            {
                switch ( errorCode )
                {
                    case OnlineError::kRateLimited:
                        return LoginResult::RateLimited;
                    case OnlineError::kUpdateRequired:
                        return LoginResult::UpdateRequired;
                    case OnlineError::kUnauthenticated:
                        return LoginResult::InvalidToken;
                    case OnlineError::kInvalidRequest:
                        return LoginResult::InvalidRequest;
                    default:
                        return LoginResult::StoreUnavailable;
                }
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
        : _mapClientIdToCall{}
        , _listReply{}
        , _listEvent{}
        , _listDeferred{}
        , _clientInfo{}
        , _identity{}
        , _token{}
        , _sendingCall{}
        , _pClient{ nullptr }
        , _nextRequestId{ 1 }
        , _bLoggedIn{ SW_FALSE }
        , _bResuming{ SW_FALSE }
        , _bSending{ SW_FALSE }
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
            reply._requestId          = _nextRequestId++;
            reply._operation          = AccountClientOperation::Resume;
            reply._result             = LoginResult::InvalidToken;
            return reply._requestId;
        }
        BitWriter body;
        AccountWire::writeToken( body, _token );
        AccountWire::writeClientInfo( body, _clientInfo );
        _bResuming = SW_TRUE;
        return send( AccountClientOperation::Resume, AccountMethod::kResume, body, false );
    }

    uint64 AccountClient::logout() { return send( AccountClientOperation::Logout, AccountMethod::kLogout, BitWriter{}, true ); }

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

    uint64 AccountClient::issueGameTicket( string_view serverId )
    {
        BitWriter body;
        AccountWire::writeText( body, serverId );
        return send( AccountClientOperation::IssueGameTicket, AccountMethod::kIssueGameTicket, body, true );
    }

    uint64 AccountClient::unlinkPlatform( string_view provider )
    {
        BitWriter body;
        AccountWire::writeText( body, provider );
        return send( AccountClientOperation::UnlinkPlatform, AccountMethod::kUnlinkPlatform, body, true );
    }

    uint64 AccountClient::listLinks() { return send( AccountClientOperation::ListLinks, AccountMethod::kListLinks, BitWriter{}, true ); }

    uint64 AccountClient::requestDeletion() { return send( AccountClientOperation::RequestDeletion, AccountMethod::kRequestDeletion, BitWriter{}, true ); }

    uint64 AccountClient::cancelDeletion() { return send( AccountClientOperation::CancelDeletion, AccountMethod::kCancelDeletion, BitWriter{}, true ); }

    int32 AccountClient::pollReplies( vector<AccountClientReply>& outListReply )
    {
        const int32 count = static_cast<int32>( _listReply.size() );
        for ( AccountClientReply& reply : _listReply )
            outListReply.push_back( std::move( reply ) );
        _listReply.clear();
        return count;
    }

    int32 AccountClient::pollEvents( vector<AccountClientEvent>& outListEvent )
    {
        const int32 count = static_cast<int32>( _listEvent.size() );
        for ( AccountClientEvent& event : _listEvent )
            outListEvent.push_back( std::move( event ) );
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

    uint16 AccountClient::getMethodRange() const { return OnlineMethodRange::kAccount; }

    uint32 AccountClient::getProtocolVersion() const { return AccountProtocol::kVersion; }

    void AccountClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != AccountMethod::kPushRevoked )
            return;
        AccountClientEvent event;
        if ( AccountWire::readRevokedPush( body, event._reason, event._reasonCode ) == false )
            return;
        _bLoggedIn = SW_FALSE;
        _token     = LoginSessionToken{}; // 밀려난 세션은 돌아올 수 없다 — 다시 로그인
        _listEvent.push_back( std::move( event ) );
    }

    void AccountClient::onClientReady( OnlineServiceClient& client )
    {
        (void)client;
        _bLoggedIn = SW_FALSE; // 새 연결 — 서버는 아직 주체를 모른다
        if ( _token.isEmpty() == false && _bResuming == SW_FALSE )
        {
            BitWriter body;
            AccountWire::writeToken( body, _token );
            AccountWire::writeClientInfo( body, _clientInfo );
            _bResuming = SW_TRUE;
            (void)send( AccountClientOperation::Resume, AccountMethod::kResume, body, false, true );
        }
    }

    uint64 AccountClient::send( AccountClientOperation operation, uint16 method, const BitWriter& body, bool bNeedsSession, bool bAutomatic )
    {
        const uint64        requestId = _nextRequestId++;
        const vector<uint8> bodyBytes( body.getBytes().begin(), body.getBytes().begin() + body.getByteCount() );
        if ( bNeedsSession && _bResuming == SW_TRUE )
        {
            _listDeferred.push_back( DeferredCall{ bodyBytes, requestId, method, operation } ); // 재접속 응답 뒤에 보낸다
            return requestId;
        }
        transmit( operation, method, bodyBytes, requestId, bAutomatic );
        return requestId;
    }

    void AccountClient::transmit( AccountClientOperation operation, uint16 method, const vector<uint8>& bodyBytes, uint64 requestId, bool bAutomatic )
    {
        BitWriter body;
        if ( bodyBytes.empty() == false )
            body.writeBytes( bodyBytes.data(), static_cast<int32>( bodyBytes.size() ) );
        NetRequestOptions options;
        _sendingCall                = PendingCall{ requestId, operation, static_cast<uint8>( bAutomatic ? SW_TRUE : SW_FALSE ) };
        _bSending                   = SW_TRUE;
        const uint64 clientId       = _pClient->sendRequest( method, body, options, OnlineResponseDelegate::create<&AccountClient::onResponse>( this ) );
        _bSending                   = SW_FALSE;
        const bool bAlreadyAnswered = _sendingCall._requestId == 0;
        if ( bAlreadyAnswered == false )
            _mapClientIdToCall[clientId] = _sendingCall;
        _sendingCall = PendingCall{};
    }

    void AccountClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        const auto  callIt = _mapClientIdToCall.find( response._requestId );
        if ( callIt != _mapClientIdToCall.end() )
        {
            call = callIt->second;
            _mapClientIdToCall.erase( callIt );
        }
        else if ( _bSending == SW_TRUE && _sendingCall._requestId != 0 )
        {
            call                    = _sendingCall;
            _sendingCall._requestId = 0; // 맡기는 자리에서 바로 끝났다
        }
        else
        {
            return;
        }

        AccountClientReply reply;
        reply._requestId  = call._requestId;
        reply._operation  = call._operation;
        reply._bAutomatic = call._bAutomatic;
        reply._errorCode  = response._errorCode;
        if ( response.isOk() == false )
        {
            reply._result = AccountClientInternal::toResult( response._errorCode );
            BitReader detail( response._pBody, response._bodySize );
            if ( response._errorCode == OnlineError::kUpdateRequired )
                (void)AccountWire::readText( detail, 512, reply._grant._storeUrl );
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
                transmit( deferred._operation, deferred._method, deferred._bodyBytes, deferred._requestId, false );
                continue;
            }
            AccountClientReply& reply = _listReply.emplace_back();
            reply._requestId          = deferred._requestId;
            reply._operation          = deferred._operation;
            reply._result             = failure;
        }
    }
} // namespace sw
