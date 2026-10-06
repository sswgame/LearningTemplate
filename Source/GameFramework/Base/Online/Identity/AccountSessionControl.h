/**
 * @file AccountSessionControl.h
 * @brief 계정의 붙어 있는 세션을 끊는 창구 — 계정 키트(GF_Server_Account)가 구현하고 게임(서버 조립)이 GM 키트 · 제재를 거는 쪽에 넘깁니다(키트끼리는 include 하지 못한다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class IAccountSessionControl
     * @brief 서비스 스레드에서 부릅니다. 다른 서버 프로세스의 세션은 서버 버스 알림(또는 그쪽의 다음 세션 확인)으로 끊긴다.
     */
    class SW_GF_API IAccountSessionControl
    {
    public:
        IAccountSessionControl()          = default;
        virtual ~IAccountSessionControl() = default;

        IAccountSessionControl( const IAccountSessionControl& )            = delete;
        IAccountSessionControl& operator=( const IAccountSessionControl& ) = delete;

        /** @brief 그 계정의 세션을 모두 끝내고 붙어 있는 클라이언트에 @p reasonCode(로컬라이제이션 키)를 알립니다. */
        virtual void revokeAccountSessions( AccountId accountId, string_view reasonCode, int64 nowMs ) = 0;
    };
} // namespace sw
