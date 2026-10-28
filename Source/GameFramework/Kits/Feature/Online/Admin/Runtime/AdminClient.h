/**
 * @file AdminClient.h
 * @brief GM 도구 키트의 클라이언트입니다. 운영 도구와 에디터 패널이 `OnlineServiceClient` 에 등록해 GM 명령을 보냅니다(`IOnlineClientService`).
 * @details 상태를 바꾸는 명령에는 멱등 키를 같이 보내며, 재시도할 때는 응답의 키를 다시 쓴다. GM 연결도 일반 계정 로그인(계정 키트)을 거치고, 권한은 서버가 명령마다 저장소에서 읽는다.
 *          플레이어 게임은 이 키트에 의존하지 않는다. 운영 도구와 에디터만 쓴다. 요청 · 응답 규칙은 `OnlineKitClient` 에 있다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/Base/Online/Service/OnlineKitClient.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Admin/Runtime/AdminProtocol.h"

namespace sw
{
    /** @class AdminClient @brief GM 명령 클라이언트입니다. */
    class SW_KIT_API AdminClient final : public OnlineKitClient<AdminProtocol, AdminReply>
    {
    public:
        /** @brief 명령 하나를 보내고 요청 번호를 반환합니다. 상태를 바꾸는 명령에서 @p key 가 비어 있으면 새로 만듭니다. */
        uint64 sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, const ReplyDelegate& onReply );

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const override { return AdminProtocol::kOnlineEntry; }
    };
} // namespace sw
