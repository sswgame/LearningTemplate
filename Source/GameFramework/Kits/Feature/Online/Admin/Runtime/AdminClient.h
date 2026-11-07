/**
 * @file AdminClient.h
 * @brief GM 도구 키트의 클라이언트입니다. 운영 도구와 에디터 패널이 `OnlineServiceClient` 에 등록해 GM 명령을 보냅니다(`OnlineProtocolClient`).
 * @details 상태를 바꾸는 명령에는 멱등 키를 같이 보내며(서술자의 호출 규칙), 재시도할 때는 응답의 키를 다시 쓴다. GM 연결도 일반 계정 로그인(계정 키트)을 거치고, 권한은 서버가 명령마다 저장소에서 읽는다.
 *          플레이어 게임은 이 키트에 의존하지 않는다. 운영 도구와 에디터만 쓴다. 도구는 명령을 실행 때 고르므로 번호로 부르는 `sendCommand` 를 둔다(메서드마다 요청 · 응답 형식이 같다).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Admin/Runtime/AdminProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    /** @class AdminClient @brief GM 명령 클라이언트입니다. */
    class SW_KIT_API AdminClient final : public OnlineProtocolClient<AdminClient, AdminProtocol>
    {
    public:
        using Reply         = OnlineResult<AdminReply>;
        using ReplyDelegate = Delegate<void( const Reply& )>;

        /** @brief 명령 하나를 보내고 요청 번호를 반환합니다. 상태를 바꾸는 명령에서 @p key 가 비어 있으면 새로 만듭니다. 표에 없는 번호는 보내지 않고 바로 `kInvalidRequest` 로 끝납니다. */
        uint64 sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, const ReplyDelegate& onReply );
    };
} // namespace sw
