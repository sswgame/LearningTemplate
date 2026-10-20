/**
 * @file AccountSessionControl.h
 * @brief 계정의 접속 세션을 끊는 인터페이스입니다. 계정 키트(GF_Server_Account)가 구현하고, 게임(서버 조립)이 GM 키트와 제재를 적용하는 쪽에 넘깁니다(키트끼리는 include 하지 못한다).
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
    class IAccountSessionControl
    {
        // 순수 인터페이스라 내보내지 않는다. 인라인 생성자와 소멸자를 GameFramework.dll 이 내보내지 않으므로 dllimport 로 해석되면 키트 링크가 깨진다(C-3).
    public:
        IAccountSessionControl()          = default;
        virtual ~IAccountSessionControl() = default;

        IAccountSessionControl( const IAccountSessionControl& )            = delete;
        IAccountSessionControl& operator=( const IAccountSessionControl& ) = delete;

        /** @brief 그 계정의 세션을 모두 끝내고 붙어 있는 클라이언트에 @p reasonCode(로컬라이제이션 키)를 알립니다. */
        virtual void revokeAccountSessions( AccountID accountID, string_view reasonCode, int64 nowMs ) = 0;
    };
} // namespace sw
