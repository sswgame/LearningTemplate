/**
 * @file AccountGMLogin.h
 * @brief 운영 도구(에디터 GM 도구 · 봇)가 서버에 계정 이름과 비밀번호로 한 번 로그인하는 도우미입니다. `AccountClient` 와 응답 꺼내기를 한 덩어리로 들고 상태를 글로 보입니다.
 * @details GM 연결도 일반 계정 로그인을 거친다. 에디터 확장(Admin)은 자기 키트만 include 할 수 있어 계정 키트를 모르므로, 두 쪽을 이어 붙이는 일은 둘을 함께 아는 조립 쪽(도구 실행 파일 · 게임)이 한다:
 *          `attach` 로 서비스 클라이언트에 계정 클라이언트를 등록하고(연결을 열기 전), 연결이 Ready 가 될 때 `onReady` 를 부르게 하고(`AdminToolConnection::bindLoginHook`), 프레임마다 `update` 를 부른다.
 *          다시 연결되면 로그인은 `AccountClient` 가 보관한 토큰으로 스스로 재접속하므로 `onReady` 는 토큰이 없을 때만 로그인을 보낸다.
 *          비밀번호는 이 객체의 메모리에만 있고 로그인 응답을 받으면(성공이든 실패든) 지운다. 계정이 서버에 없으면 가입하지 않는다(GM 계정은 운영이 만든다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountClient.h"

namespace sw
{
    /** @brief 로그인 도우미의 상태입니다. */
    enum class AccountGMLoginState : uint8
    {
        Idle = 0, ///< 아직 로그인을 보내지 않았다(연결이 Ready 가 되기 전이거나 `attach` 전)
        LoggingIn,
        LoggedIn,
        Failed ///< 서버가 거절했거나 전송 오류다. `getFailureText` 가 이유를 적는다. 다시 시도하려면 `setCredentials` 부터
    };

    /**
     * @class AccountGMLogin
     * @brief 계정 이름 · 비밀번호 로그인 한 번을 맡습니다.
     */
    class SW_KIT_API AccountGMLogin
    {
    public:
        AccountGMLogin();
        AccountGMLogin( const AccountGMLogin& )            = delete; // 서비스 클라이언트가 계정 클라이언트를 가리킨다
        AccountGMLogin& operator=( const AccountGMLogin& ) = delete;

        /** @brief 서비스 클라이언트에 계정 클라이언트를 등록합니다. 연결을 열기(`initialize`) 전에 부릅니다. 영역이 겹치면 false 입니다. */
        [[nodiscard]] bool attach( OnlineServiceClient& client, const AccountClientInfo& info );
        /** @brief 로그인할 계정을 정합니다. 상태가 `Idle` 로 돌아가 다음 `onReady` 가 보냅니다. 빈 이름이나 빈 비밀번호는 false 입니다(빈 값으로 로그인하지 않는다). */
        [[nodiscard]] bool setCredentials( string_view loginName, string_view password );
        /** @brief 연결이 Ready 가 될 때마다 부릅니다. 계정을 정했고 토큰이 없으면 로그인을 보냅니다. */
        void onReady( OnlineServiceClient& client );
        /** @brief 응답을 꺼내 상태를 갱신합니다. 프레임마다 부릅니다. */
        void update();

        AccountGMLoginState getState() const { return _state; }
        /** @brief 실패 이유를 글로 적습니다(실패가 아니면 빈 글). */
        const string&  getFailureText() const { return _failureText; }
        AccountClient& getAccountClient() { return _account; }

    private:
        AccountClient       _account;
        string              _loginName;
        string              _password;
        string              _failureText;
        AccountGMLoginState _state;
        bool                _bAttached;
    };
} // namespace sw
