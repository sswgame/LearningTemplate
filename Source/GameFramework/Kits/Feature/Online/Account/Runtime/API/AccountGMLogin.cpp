#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountGMLogin.h"

namespace sw
{
    SW_LOG_CALLER( "AccountGMLogin" );

    AccountGMLogin::AccountGMLogin()
        : _account{}
        , _loginName{}
        , _password{}
        , _failureText{}
        , _state{ AccountGMLoginState::Idle }
        , _bAttached{ false }
    {
    }

    bool AccountGMLogin::attach( OnlineServiceClient& client, const AccountClientInfo& info )
    {
        if ( _bAttached )
            return true;
        _account.initialize( &client, info );
        if ( client.registerClientService( &_account ) == false )
        {
            SW_LOG_ERROR( "The account client could not be registered (method range already taken)" );
            return false;
        }
        _bAttached = true;
        return true;
    }

    bool AccountGMLogin::setCredentials( string_view loginName, string_view password )
    {
        if ( loginName.empty() || password.empty() )
        {
            SW_LOG_ERROR( "A GM login needs both an account name and a password" );
            return false;
        }
        _loginName = string( loginName );
        _password  = string( password );
        _failureText.clear();
        _state = AccountGMLoginState::Idle;
        return true;
    }

    void AccountGMLogin::onReady( OnlineServiceClient& client )
    {
        (void)client; // 계정 클라이언트가 attach 에서 받은 서비스 클라이언트로 보낸다
        const bool bShouldLogIn = _bAttached && _state == AccountGMLoginState::Idle && _loginName.empty() == false && _account.getToken().isEmpty();
        if ( bShouldLogIn == false )
            return; // 토큰이 있으면 다시 연결된 것이고, 재접속은 계정 클라이언트가 스스로 한다
        _state = AccountGMLoginState::LoggingIn;
        (void)_account.login( _loginName, _password ); // 결과는 update 가 응답으로 읽는다
    }

    void AccountGMLogin::update()
    {
        if ( _bAttached == false )
            return;
        vector<AccountClientReply> listReply;
        (void)_account.pollReplies( listReply );
        for ( const AccountClientReply& reply : listReply )
        {
            const bool bLoginReply = reply._operation == AccountClientOperation::Login || reply._operation == AccountClientOperation::Resume;
            if ( bLoginReply == false )
                continue;
            _password.clear(); // 응답이 왔으면 비밀번호는 더 쓰지 않는다
            if ( reply._errorCode == 0 && reply._result == LoginResult::Ok )
            {
                _state = AccountGMLoginState::LoggedIn;
                _failureText.clear();
                continue;
            }
            _state       = AccountGMLoginState::Failed;
            _failureText = "login refused: error " + to_string( static_cast<int32>( reply._errorCode ) ) + ", result " + to_string( static_cast<int32>( reply._result ) );
        }
    }
} // namespace sw
