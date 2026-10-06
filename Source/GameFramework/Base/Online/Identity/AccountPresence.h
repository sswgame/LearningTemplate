/**
 * @file AccountPresence.h
 * @brief 서버 여럿의 접속 상태 창구 — 다른 서버 프로세스에 붙은 계정을 표시 이름으로 찾고(비동기 — 캐시), 그 계정에게 알림을 보냅니다(버스).
 * @details 계정 키트(서버)가 구현한다(`OnlinePresence` — 캐시 `presence:` 키 · 버스 `push.<서버>`). 거래 · 채팅 같은 다른 키트는 이 창구만 본다(키트끼리 include 하지 못한다).
 *          서버 한 대면 없어도 된다(nullptr) — 이 프로세스의 `IAccountDirectory` 로 충분하다. 알림은 최대 한 번(버스)이다 — 정본은 저장소.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitWriter;

    /** @brief 이름으로 찾은 결과 하나입니다. */
    struct AccountPresenceResult
    {
        AccountIdentity _identity{}; ///< 못 찾았으면 계정 id 0
        uint64          _requestId{ 0 };
        uint64          _serverId{ 0 }; ///< 그 계정이 붙어 있는 서버
    };
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

        /** @brief 접속한 계정을 표시 이름(대소문자 무시)으로 찾습니다. 0 이 아닌 요청 id — 결과는 `pollFound` 로 한 번. */
        virtual uint64 submitFindByDisplayName( string_view displayName )        = 0;
        virtual int32  pollFound( vector<AccountPresenceResult>& outListResult ) = 0;
        /** @brief 다른 서버에 붙은 계정에게 알림(`[종류][몸]`)을 맡깁니다. 그 계정의 서버를 모르면 false. */
        virtual bool sendRemotePush( AccountId accountId, uint16 kind, const BitWriter& body ) = 0;
    };
} // namespace sw
