/**
 * @file ServerBus.h
 * @brief 서버 프로세스끼리의 알림 — 주제(topic) 발행/구독, 최대 한 번 배달입니다.
 * @details 놓치면 안 되는 것은 영속 저장 + 다시 읽기(버스는 "빨리 알기" 용). 받은 메시지에 자기 서버가 보낸 것도 온다(`_originServerId` 로 거른다).
 *          주제는 `[0-9a-z_.]`(200 B 이하) — "account.revoke" · "config.changed" · "chat.channel.<이름>". 틀린 주제의 발행 · 구독은 경고를 남기고 버린다.
 *          구현: `EphemeralServerBus`(캐시 발행/구독 위 — 서버 여럿), `LocalServerBus`(프로세스 안 — 서버 한 대 · 시험).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 버스 메시지 하나입니다. */
    struct ServerBusMessage
    {
        vector<uint8> _bytes{};
        string        _topic{};
        uint64        _originServerId{ 0 };
        uint64        _sequence{ 0 }; ///< 보낸 서버 안의 순번(1 부터 — 빠짐 감지, 진단만)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IServerBus
     * @brief 서버 간 버스의 앞입니다 — 서비스 스레드 하나가 발행하고 틱마다 거둔다.
     *        서비스는 `pollMessages` 를 부르지 않는다 — 호스트가 비우고 `subscribeServerBus` 한 서비스에 나눠 준다(소비자가 둘이면 서로의 메시지를 가져간다).
     */
    class SW_GF_API IServerBus
    {
    public:
        static constexpr int32 kMaxTopicSize   = 200;
        static constexpr int32 kMaxMessageSize = 60 * 1024; ///< 봉투를 얹어도 캐시 값 상한(64 KiB) 안

        IServerBus()          = default;
        virtual ~IServerBus() = default;

        IServerBus( const IServerBus& )            = delete;
        IServerBus& operator=( const IServerBus& ) = delete;

        virtual void publish( string_view topic, const uint8* pData, int32 size ) = 0;
        virtual void subscribe( string_view topic )                               = 0;
        virtual void unsubscribe( string_view topic )                             = 0;
        /** @brief 받은 메시지를 뒤에 붙입니다(서비스 스레드 — 틱마다). 붙인 수입니다. */
        virtual int32  pollMessages( vector<ServerBusMessage>& outListMessage ) = 0;
        virtual uint64 getServerId() const                                      = 0;

        /** @brief 주제 규칙(`[0-9a-z_.]`, 1..200 B)을 지키는가입니다. */
        static bool isValidTopic( string_view topic );
    };
} // namespace sw
