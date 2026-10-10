/**
 * @file AccountPresence.h
 * @brief 서버 여럿의 접속 상태 창구 — 계정이 지금 어느 서버에 붙어 있는지(계정 id 로) · 접속한 계정을 표시 이름으로 찾고(비동기 — 캐시), 다른 서버의 계정에게 알림을 보냅니다(버스).
 * @details - 계정 키트(서버)가 구현한다(`OnlinePresence` — 캐시 `presence:` 키 · 버스 `push.<서버>`). 거래 · 채팅 · 친구 · 매칭 같은 다른 키트는 이 창구만 본다
 *            (키트끼리 include 하지 못한다 — 기록 형식은 구현 하나가 갖는다).
 *          - 찾기 결과는 맡긴 델리게이트로 정확히 한 번 온다(`cancel` 한 것은 빼고, 호스트 `tick` 안 — 맡긴 함수 안에서 바로 부르지 않는다). 여러 서비스가 같이 찾아도 서로의 결과를 가져가지 않는다.
 *          - 서버 한 대면 없어도 된다(nullptr) — 이 프로세스의 `IAccountDirectory` 로 충분하다. 알림은 최대 한 번(버스)이다 — 정본은 저장소.
 *          언리얼 `IOnlinePresence::QueryPresence`(사용자 id → 상태, 완료 델리게이트) · Nakama status registry 와 같은 자리다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitWriter;

    /** @brief 찾은 결과 하나입니다. 접속해 있지 않으면 `_serverID` 가 0 입니다. */
    struct AccountPresenceResult
    {
        AccountIdentity _identity{}; ///< 이름으로 찾았으면 공개 신원, 계정 id 로 찾았으면 계정 id 만(못 찾았으면 계정 id 0)
        uint64          _requestID{ 0 };
        uint64          _serverID{ 0 }; ///< 그 계정이 붙어 있는 서버(이 서버일 수도 있다)

        bool isOnline() const { return _serverID != 0; }
    };

    using AccountPresenceDelegate = Delegate<void( const AccountPresenceResult& )>;
} // namespace sw

namespace sw
{
    /**
     * @class IAccountPresence
     * @brief 접속 상태 창구입니다(서비스 스레드).
     */
    class SW_GF_API IAccountPresence
    {
    public:
        IAccountPresence()          = default;
        virtual ~IAccountPresence() = default;

        IAccountPresence( const IAccountPresence& )            = delete;
        IAccountPresence& operator=( const IAccountPresence& ) = delete;

        /** @brief 접속한 계정을 표시 이름(대소문자 무시)으로 찾습니다. 0 이 아닌 요청 id — 결과는 @p onFound 로 한 번. */
        virtual uint64 submitFindByDisplayName( string_view displayName, const AccountPresenceDelegate& onFound ) = 0;
        /** @brief 계정이 지금 붙어 있는 서버를 찾습니다(친구 접속 표시 · 다른 서버 귓속말 · 매칭 결과를 보낼 곳). 0 이 아닌 요청 id — 결과는 @p onFound 로 한 번. */
        virtual uint64 submitFindByAccount( AccountID accountID, const AccountPresenceDelegate& onFound ) = 0;
        /** @brief 다른 서버에 붙은 계정에게 알림(`[종류][몸]`)을 맡깁니다(그 계정의 서버를 찾아 넘긴다 — 접속해 있지 않으면 버린다). 서버 여럿이 아니면 false. */
        virtual bool sendRemotePush( AccountID accountID, uint16 kind, const BitWriter& body ) = 0;
        /**
         * @brief 맡긴 찾기를 취소합니다 — 그 델리게이트는 불리지 않는다. 델리게이트 주인이 이 창구보다 먼저 내려갈 때 자기 `shutdown` 에서 부른다
         *        (캐시 라우터의 `cancel` 과 같은 계약). 끝났거나 모르는 id 면 아무것도 하지 않는다.
         */
        virtual void cancel( uint64 requestID ) = 0;
    };
} // namespace sw
