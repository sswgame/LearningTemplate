/**
 * @file IOfflinePushSender.h
 * @brief 서버의 오프라인 푸시 포트입니다 — 접속하지 않은 계정의 기기에 알림을 보낸다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"
#include "GameFramework/Runtime/Online/Service/OnlineServerKitName.h"

namespace sw
{
    class IServiceStoreConnection;
    enum class ServiceStoreResult : uint8;

    // 접속해 있지 않은 계정에게 기기 푸시를 보내는 포트 — 라이브 운영 키트가 푸시 발송기로 구현해 서버 조립 문맥(`OnlineServerKitContext::_kits`)에 내놓고,
    // 우편함 키트(새 우편)처럼 "오프라인이면 푸시" 를 정하는 키트가 부른다. 키트끼리 include 하지 않도록 글은 로컬라이제이션 키로만 넘긴다.
    // 접속해 있는지는 부르는 쪽이 정한다(접속한 계정에게는 게임 알림이 먼저다). 서비스 스레드 하나에서만 쓴다.
    /**
     * @class IOfflinePushSender
     * @brief 오프라인 계정의 기기 푸시 창입니다.
     */
    class IOfflinePushSender
    {
    public:
        static constexpr const utf8* kProviderKit = OnlineServerKitName::kLiveOps; ///< 이 인터페이스를 내는 키트다. 받는 키트의 의존 이름이 여기서 나온다(`OnlineServerKit`)

        IOfflinePushSender()          = default;
        virtual ~IOfflinePushSender() = default;

        IOfflinePushSender( const IOfflinePushSender& )            = delete;
        IOfflinePushSender& operator=( const IOfflinePushSender& ) = delete;

        /** @brief @p accountID 의 모든 기기에 보냅니다. 계정마다 도배 제한에 걸리면 보내지 않고 false 입니다. @p deepLink 는 앱이 열 화면(예: "mailbox")입니다. */
        virtual bool sendOfflinePush( AccountID accountID, string_view titleKey, string_view bodyKey, string_view deepLink, UnixMilliseconds nowMs ) = 0;
    };
} // namespace sw
