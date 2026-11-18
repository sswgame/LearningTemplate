#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountClient.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetEndpointSecurity.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    namespace
    {
        struct AccountClientInternal
        {
            static bool isGrantOperation( AccountMethodName operation )
            {
                return operation == AccountMethodName::Login || operation == AccountMethodName::GuestLogin ||
                       operation == AccountMethodName::PlatformLogin || operation == AccountMethodName::Resume;
            }

            /** @brief 서술자 번호(와이어 메서드 번호)를 요청 종류로 바꿉니다. 번호는 표의 열거자 순서다(`OnlineMethodTable::toMethod` 의 역). */
            static AccountMethodName toOperation( uint16 method )
            {
                SW_ASSERT( AccountMethod::kFirstMethod <= method && method < AccountMethod::kFirstMethod + AccountMethod::kCount );
                return static_cast<AccountMethodName>( method - AccountMethod::kFirstMethod );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AccountClient::AccountClient()
        : _replyBuffer{}
        , _eventBuffer{}
        , _clientInfo{}
        , _identity{}
        , _token{}
        , _pServiceClient{ nullptr }
        , _bLoggedIn{ SW_FALSE }
        , _bResuming{ SW_FALSE }
        , _bAutomaticResume{ SW_FALSE }
    {
    }

    void AccountClient::initialize( OnlineServiceClient* pClient, const AccountClientInfo& clientInfo )
    {
        OnlineProtocolClient::initialize( pClient );
        _pServiceClient = pClient;
        _clientInfo     = clientInfo;
    }

    uint64 AccountClient::registerAccount( string_view loginName, string_view password )
    {
        return call<AccountMethod::Register>( AccountCredentialRequest{ string( loginName ), string( password ) },
                                              OnlineReplyDelegate<AccountMethod::Register>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::login( string_view loginName, string_view password )
    {
        return call<AccountMethod::Login>( AccountLoginRequest{ string( loginName ), string( password ), _clientInfo }, OnlineReplyDelegate<AccountMethod::Login>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize] )
    {
        AccountGuestLoginRequest request;
        Memory::copy( request._arrDeviceSecret, arrDeviceSecret, LoginConstant::kDeviceSecretSize );
        request._clientInfo = _clientInfo;
        return call<AccountMethod::GuestLogin>( request, OnlineReplyDelegate<AccountMethod::GuestLogin>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::platformLogin( string_view provider, const vector<uint8>& ticketBytes )
    {
        return call<AccountMethod::PlatformLogin>( AccountPlatformRequest{ string( provider ), ticketBytes, _clientInfo },
                                                   OnlineReplyDelegate<AccountMethod::PlatformLogin>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::resume()
    {
        if ( _token.isEmpty() )
        {
            _replyBuffer.push( AccountClientReply{} );
            AccountClientReply& reply = _replyBuffer.getLast();
            reply._requestID          = allocateRequestID();
            reply._operation          = AccountMethodName::Resume;
            reply._result             = LoginResult::InvalidToken;
            return reply._requestID;
        }
        return sendResume( false );
    }

    uint64 AccountClient::logout()
    {
        return call<AccountMethod::Logout>( OnlineReplyDelegate<AccountMethod::Logout>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::linkCredential( string_view loginName, string_view password )
    {
        return call<AccountMethod::LinkCredential>( AccountCredentialRequest{ string( loginName ), string( password ) },
                                                    OnlineReplyDelegate<AccountMethod::LinkCredential>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::linkPlatform( string_view provider, const vector<uint8>& ticketBytes )
    {
        return call<AccountMethod::LinkPlatform>( AccountPlatformRequest{ string( provider ), ticketBytes, AccountClientInfo{} },
                                                  OnlineReplyDelegate<AccountMethod::LinkPlatform>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::issueGameTicket( string_view serverID )
    {
        return call<AccountMethod::IssueGameTicket>( AccountGameTicketRequest{ string( serverID ) }, OnlineReplyDelegate<AccountMethod::IssueGameTicket>::create<&AccountClient::onTicketReply>( this ) );
    }

    uint64 AccountClient::unlinkPlatform( string_view provider )
    {
        return call<AccountMethod::UnlinkPlatform>( AccountProviderRequest{ string( provider ) }, OnlineReplyDelegate<AccountMethod::UnlinkPlatform>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::listLinks()
    {
        return call<AccountMethod::ListLinks>( OnlineReplyDelegate<AccountMethod::ListLinks>::create<&AccountClient::onLinkReply>( this ) );
    }

    uint64 AccountClient::requestDeletion()
    {
        return call<AccountMethod::RequestDeletion>( OnlineReplyDelegate<AccountMethod::RequestDeletion>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::cancelDeletion()
    {
        return call<AccountMethod::CancelDeletion>( OnlineReplyDelegate<AccountMethod::CancelDeletion>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::issueLoginNonce()
    {
        return call<AccountMethod::IssueLoginNonce>( OnlineReplyDelegate<AccountMethod::IssueLoginNonce>::create<&AccountClient::onNonceReply>( this ) );
    }

    int32 AccountClient::pollReplies( vector<AccountClientReply>& outListReply )
    {
        const int32 count = static_cast<int32>( _replyBuffer.getCount() );
        _replyBuffer.moveAllTo( outListReply );
        return count;
    }

    int32 AccountClient::pollEvents( vector<AccountClientEvent>& outListEvent )
    {
        const int32 count = static_cast<int32>( _eventBuffer.getCount() );
        _eventBuffer.moveAllTo( outListEvent );
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

    void AccountClient::onPush( AccountMethod::Revoked, const AccountRevokedPush& body )
    {
        AccountClientEvent event;
        event._reason     = body._reason;
        event._reasonCode = body._reasonCode;
        _bLoggedIn        = SW_FALSE;
        _token            = LoginSessionToken{}; // 밀려난 세션은 돌아올 수 없으므로 다시 로그인해야 한다
        _eventBuffer.push( std::move( event ) );
    }

    void AccountClient::onClientReady( OnlineServiceClient& client )
    {
        (void)client;
        _bLoggedIn = SW_FALSE; // 새 연결이므로 서버는 아직 주체를 모른다
        if ( _token.isEmpty() == false && _bResuming == SW_FALSE )
            (void)sendResume( true );
    }

    void AccountClient::onClientDisconnected( OnlineServiceClient& client )
    {
        (void)client;
        _bLoggedIn = SW_FALSE;
    }

    uint64 AccountClient::sendResume( bool bAutomatic )
    {
        if ( _pServiceClient != nullptr )
            _pServiceClient->holdSessionRequests(); // 모든 키트의 세션 요청(이 키트 포함)은 재접속 응답 뒤에 간다. 재접속 자신은 익명이라 바로 간다
        _bResuming        = SW_TRUE;
        _bAutomaticResume = bAutomatic ? SW_TRUE : SW_FALSE;
        return call<AccountMethod::Resume>( AccountResumeRequest{ _token, _clientInfo }, OnlineReplyDelegate<AccountMethod::Resume>::create<&AccountClient::onGrantReply>( this ) );
    }

    AccountClientReply AccountClient::makeReply( uint64 requestID, uint16 method, uint16 errorCode, const vector<uint8>& errorDetail )
    {
        AccountClientReply reply;
        reply._requestID = requestID;
        reply._operation = AccountClientInternal::toOperation( method );
        reply._errorCode = errorCode;
        if ( errorCode == OnlineError::kOk )
            return reply;
        reply._result = LoginServiceResult::fail( errorCode );
        BitReader detail( errorDetail.data(), static_cast<int32>( errorDetail.size() ) );
        if ( errorCode == OnlineError::kUpdateRequired )
        {
            AccountUpdateDetail updateDetail;
            if ( errorDetail.empty() == false && AccountProtocol::MessageTypes::read( detail, updateDetail ) == FixedReadResult::Ok )
                reply._grant._storeURL = std::move( updateDetail._storeURL ); // 상세가 없거나 깨졌으면 상점 주소만 빈다(스키마 해시가 다를 때도 그렇다)
        }
        else if ( errorCode == OnlineError::kRateLimited && errorDetail.empty() == false )
        {
            reply._grant._retryAfterMs = detail.readVarUint63();
        }
        return reply;
    }

    void AccountClient::onGrantReply( const OnlineResult<AccountGrantReply>& result )
    {
        AccountClientReply reply = makeReply( result._requestID, result._method, result._errorCode, result._errorDetail );
        if ( result._errorCode == OnlineError::kOk )
        {
            reply._result = result._reply._result;
            reply._grant  = result._reply._grant;
        }
        const bool bOk = reply._result == LoginResult::Ok;
        if ( bOk && AccountClientInternal::isGrantOperation( reply._operation ) )
        {
            _bLoggedIn = SW_TRUE;
            _token     = reply._grant._token;
            _identity  = reply._grant._identity;
        }
        if ( bOk && ( reply._operation == AccountMethodName::LinkCredential || reply._operation == AccountMethodName::LinkPlatform ) )
            _identity = reply._grant._identity;
        if ( bOk && ( reply._operation == AccountMethodName::Logout || reply._operation == AccountMethodName::RequestDeletion ) )
        {
            _bLoggedIn = SW_FALSE;
            _token     = LoginSessionToken{};
        }
        if ( reply._operation != AccountMethodName::Resume )
        {
            _replyBuffer.push( std::move( reply ) );
            return;
        }
        reply._bAutomatic = _bAutomaticResume;
        _replyBuffer.push( reply );
        finishResume( reply );
    }

    void AccountClient::onTicketReply( const OnlineResult<AccountTicketReply>& result )
    {
        AccountClientReply reply = makeReply( result._requestID, result._method, result._errorCode, result._errorDetail );
        if ( result._errorCode == OnlineError::kOk )
        {
            reply._result = result._reply._result;
            reply._ticket = result._reply._ticket;
        }
        _replyBuffer.push( std::move( reply ) );
    }

    void AccountClient::onNonceReply( const OnlineResult<AccountNonceReply>& result )
    {
        AccountClientReply reply = makeReply( result._requestID, result._method, result._errorCode, result._errorDetail );
        if ( result._errorCode == OnlineError::kOk )
        {
            reply._result     = result._reply._result;
            reply._loginNonce = result._reply._nonce;
        }
        _replyBuffer.push( std::move( reply ) );
    }

    void AccountClient::onLinkReply( const OnlineResult<AccountLinkReply>& result )
    {
        AccountClientReply reply = makeReply( result._requestID, result._method, result._errorCode, result._errorDetail );
        if ( result._errorCode == OnlineError::kOk )
        {
            reply._result      = result._reply._result;
            reply._linkSummary = result._reply._summary;
        }
        _replyBuffer.push( std::move( reply ) );
    }

    void AccountClient::finishResume( const AccountClientReply& reply )
    {
        const bool bTokenDead = reply._result == LoginResult::InvalidToken || reply._result == LoginResult::Expired || reply._result == LoginResult::Revoked;
        if ( bTokenDead )
            _token = LoginSessionToken{};
        _bResuming        = SW_FALSE;
        _bAutomaticResume = SW_FALSE;
        if ( _pServiceClient == nullptr || _pServiceClient->isHoldingSessionRequests() == false )
            return;
        // 모은 세션 요청은 재접속의 결과로 끝난다 — 저장소 장애(공통 범주)를 "로그인 안 됨" 으로 바꾸면 산 세션을 죽었다고 알린다
        uint16 failureCode = OnlineError::kOk;
        if ( reply._result.isFailed() )
            failureCode = reply._result._errorCode;
        else if ( reply._result != LoginResult::Ok )
            failureCode = OnlineError::kUnauthenticated;
        _pServiceClient->releaseSessionRequests( failureCode );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct AccountClientRegistration
        {
            /** @brief 계정 클라이언트는 로그인에 빌드 글 · 플랫폼을 싣는다. */
            static unique_ptr<IOnlineClientService> create( OnlineServiceClient& client, const OnlineSessionInfo& info )
            {
                AccountClientInfo clientInfo;
                clientInfo._build                 = info._gameBuild;
                clientInfo._platform              = info._platform;
                unique_ptr<AccountClient> pClient = make_unique<AccountClient>();
                pClient->initialize( &client, clientInfo );
                return pClient;
            }
        };
    } // namespace

    SW_REGISTER_ONLINE_CLIENT_WITH( AccountClient, &AccountClientRegistration::create );
} // namespace sw
