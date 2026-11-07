/**
 * @file ServerDirectoryClient.h
 * @brief 서버 디렉터리 클라이언트입니다. 요청은 `call<ServerDirectoryMethod::GetStatus>( onReply )` 이고, 이 클래스는 응답과 알림으로 받은 마지막 상태를 보관합니다.
 * @details - 요청 · 응답 규칙은 `OnlineProtocolClient` 에 있다. 오류 코드(`_errorCode`)는 `OnlineError::kOk`, 공통 오류, 키트 오류(`ServerDirectoryError`) 중 하나이다.
 *          - 상태는 로그인 전에도 묻는다. 서버 고르기 화면과 점검 안내 화면이 쓴다. 배정은 로그인한 뒤에 한다.
 *
 *          언리얼 Online Services 의 비동기 호출과 완료 델리게이트에 해당한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    /**
     * @class ServerDirectoryClient
     * @brief 연결 하나의 서버 디렉터리 클라이언트입니다. 가벼우므로 부하 테스트 봇은 봇마다 하나씩 둡니다.
     */
    class SW_KIT_API ServerDirectoryClient final : public OnlineProtocolClient<ServerDirectoryClient, ServerDirectoryProtocol>
    {
    public:
        ServerDirectoryClient();

        /** @brief 마지막으로 받은 상태입니다. 응답이나 알림으로 받은 것입니다. */
        const ServerDirectoryStatus& getLastStatus() const { return _lastStatus; }
        /** @brief 상태를 받을 때마다 올라가는 수입니다. 화면이 다시 그릴지 판단하는 데 씁니다. */
        uint64 getStatusRevision() const { return _statusRevision; }

        // OnlineProtocolClient 의 훅
        using OnlineProtocolClient::onCallSucceeded;
        void onCallSucceeded( ServerDirectoryMethod::GetStatus, const OnlineNoBody& request, const ServerDirectoryStatus& reply );
        void onPush( ServerDirectoryMethod::Status, const ServerDirectoryStatus& body );

    private:
        ServerDirectoryStatus _lastStatus;
        uint64                _statusRevision;
    };
} // namespace sw
