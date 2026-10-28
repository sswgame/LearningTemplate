/**
 * @file AdminLoginBridge.h
 * @brief GM 도구 로그인 조립입니다 - GM Tool 패널(`GF_EditorPlugin_Admin`)의 계정 이름 · 비밀번호 칸을 계정 키트의 로그인 도우미(`AccountGmLogin`)에 잇습니다.
 * @details GM 연결도 일반 계정 로그인을 거친다. Admin 확장은 자기 키트(GM)만 알아 계정 키트를 모르므로, 두 키트를 함께 아는 이 확장이 이어 붙인다(OnlineKits I6):
 *          - 패널이 연결을 누르면 `AdminToolSession::connectWithLogin` 이 입력 칸의 값을 이 서브시스템의 처리기(`acceptCredentials`)에 넘긴다 → `AccountGmLogin::setCredentials`.
 *          - 연결이 Ready 가 될 때마다(`AdminToolConnection::bindLoginHook`) `AccountGmLogin::onReady` 가 로그인을 보낸다. 다시 연결된 경우는 계정 클라이언트가 보관한 토큰으로 스스로 재접속한다.
 *          - 프레임마다(`AdminToolSession::bindFrameHook`) 응답을 꺼내 상태를 갱신한다. 비밀번호는 응답을 받으면 지운다.
 *          한 계정은 연결 하나에만 붙으므로 GM 계정은 일반 플레이어 계정과 따로 쓴다. 계정 클라이언트 정보는 플랫폼 `editor` · 빌드 `1.0.0` 이다 - 서버의 빌드 확인은 플랫폼별 설정이 있을 때만 걸리고
 *          `editor` 플랫폼에는 운영이 최소 빌드를 두지 않는다.
 *          이 서브시스템은 Admin 확장의 `AdminToolSession` 을 인라인 멤버로만 부른다(확장 DLL 끼리는 링크하지 않는다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"

#include "Editor/Common/EditorSubsystem.h"

#include "GameFramework/Kits/Feature/Online/Account/Shared/API/AccountGmLogin.h"

namespace sw
{
    class OnlineServiceClient;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AdminLoginBridge
     * @brief GM Tool 의 로그인 조립 서브시스템입니다.
     */
    class AdminLoginBridge final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "AdminLoginBridge";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief GM 도구 계정 클라이언트 정보의 플랫폼 · 빌드입니다(파일 머리말). */
        static constexpr const utf8* kGmToolPlatform = "editor";
        static constexpr const utf8* kGmToolBuild    = "1.0.0";

        AdminLoginBridge();
        AdminLoginBridge( const AdminLoginBridge& )            = delete; // 서비스 클라이언트가 계정 클라이언트를 가리킨다
        AdminLoginBridge& operator=( const AdminLoginBridge& ) = delete;

        /** @brief GM Tool 세션보다 뒤에 선다(세션에 처리기 · 훅을 걸어야 한다). */
        span<const utf8* const> getSubsystemDependencies() const override;
        /** @brief 세션에 처리기 · 로그인 훅 · 프레임 훅을 걸고 계정 클라이언트를 연결에 등록합니다. 세션이 없거나 등록이 실패하면 실패입니다(조립이 반쪽이면 서지 않는다). */
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;

        AccountGmLogin&       getLogin() { return _login; }
        const AccountGmLogin& getLogin() const { return _login; }

    private:
        bool acceptCredentials( string_view loginName, string_view password );
        void onReady( OnlineServiceClient& client );
        void onFrame();

        AccountGmLogin _login;
        Subscription   _credentialsSubscription;
        Subscription   _loginHookSubscription;
        Subscription   _frameHookSubscription;
    };
} // namespace sw::editor
