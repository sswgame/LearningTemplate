/**
 * @file EphemeralServerBus.h
 * @brief 캐시 층(`IEphemeralStore`)의 발행/구독 위 서버 간 버스입니다. 서버 여럿이 같은 캐시를 볼 때 씁니다.
 * @details - 채널 이름은 `bus:<주제>`, 봉투는 `[버전 1][서버 id 8][순번 8][본문]`(리틀 엔디언)입니다.
 *            봉투는 바꾸지 않는 고정 형식입니다. 버전을 바꾸면 배포 중 섞인 동안 모든 버스 메시지가 버려집니다(`getDroppedCount` 만 셉니다).
 *          - 캐시 앞은 호스트의 라우터(`EphemeralStoreRouter`)로 나눠 씁니다. 버스는 자기 채널의 메시지만 라우터 구독으로 받고, 발행 답은 라우터가 버립니다.
 *            그래서 같은 캐시 앞을 서비스들(접속 상태, 매칭, 순위표)과 버스가 함께 씁니다. 캐시 앞의 답과 메시지를 꺼내는 것은 라우터 하나입니다.
 *          - 라우터의 `pump` 가 메시지를 버스 큐에 넣고, `pollMessages` 가 큐를 비웁니다. 호스트 `tick` 이 이 순서로 부릅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct EphemeralMessage;

    class EphemeralStoreRouter;

    /**
     * @class EphemeralServerBus
     * @brief 캐시 발행/구독 위 버스 하나(서버 하나)입니다. 서비스 스레드 하나에서 씁니다.
     */
    class SW_GF_API EphemeralServerBus final : public IServerBusTransport
    {
    public:
        /** @brief @p pRouter 는 빌려 씁니다. 버스보다 오래 살아야 하며, 구독은 라우터가 초기화된 뒤에만 됩니다. null 이면 `bindRouter` 로 나중에 정합니다. */
        EphemeralServerBus( EphemeralStoreRouter* pRouter, uint64 serverID );
        ~EphemeralServerBus() override;

        /**
         * @brief 라우터를 정합니다. 호스트가 라우터를 여는 것은 호스트 `initialize` 안이고, 호스트는 그 전에 버스 포인터를 받으므로 서버 조립은 버스를 먼저 만들고
         *        호스트를 띄운 뒤 이것을 부릅니다. 구독이 있으면 바꾸지 않습니다(false).
         */
        [[nodiscard]] bool bindRouter( EphemeralStoreRouter* pRouter );

        void   publish( string_view topic, const uint8* pData, int32 size ) override;
        void   subscribe( string_view topic ) override;
        void   unsubscribe( string_view topic ) override;
        int32  pollMessages( vector<ServerBusMessage>& outListMessage ) override;
        uint64 getServerID() const override { return _serverID; }

        /** @brief 버린 봉투(깨진 것, 버전이 다른 것)의 수입니다(진단). */
        uint64 getDroppedCount() const { return _droppedCount; }

    private:
        struct TopicSubscription
        {
            string       _topic{};
            Subscription _subscription{}; ///< 라우터의 채널 구독. 줄이 지워지면 풀린다
        };

        /** @brief 라우터가 이 버스의 채널 메시지 하나를 넘겨줍니다. 봉투를 벗겨 큐에 넣습니다. */
        void onChannelMessage( const EphemeralMessage& message );

        vector<TopicSubscription> _listSubscription;
        vector<ServerBusMessage>  _listQueuedMessage; ///< 라우터가 넣고 `pollMessages` 가 비운다
        EphemeralStoreRouter*     _pRouter;
        uint64                    _serverID;
        uint64                    _nextSequence;
        uint64                    _droppedCount;
    };
} // namespace sw
