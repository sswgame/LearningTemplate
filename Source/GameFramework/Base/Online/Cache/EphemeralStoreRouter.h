/**
 * @file EphemeralStoreRouter.h
 * @brief 캐시 앞(`IEphemeralStore`) 하나를 여러 서비스가 나눠 씁니다 — 요청마다 답 델리게이트, 채널마다 구독자 목록. 앞의 답 · 메시지를 꺼내는 쪽은 이 라우터 하나뿐이어야 합니다.
 * @details - 서비스 스레드 하나에서 쓴다(`submit` · `subscribe` · `pump` 모두). 델리게이트는 `pump` 안에서 불린다.
 *          - 델리게이트 안에서 `submit` · `subscribe` · `unsubscribe` 를 불러도 된다(지금 나눠 주는 목록은 복사본).
 *          - 같은 채널을 둘이 구독하면 앞에는 한 번만 구독하고, 마지막 구독자가 나갈 때 푼다.
 *          - `shutdown` 은 답을 기다리는 요청마다 `Unavailable` 답을 한 번 부른다 — 델리게이트는 언제나 정확히 한 번.
 *          `IEphemeralStore::pollReplies` · `pollMessages` 는 앞 전체의 것을 꺼내므로, 서비스 둘이 직접 부르면 서로의 답을 가져간다(gRPC · Nakama 런타임처럼 런타임이 나눠 준다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class EphemeralStoreRouter
     * @brief 캐시 앞의 답 · 메시지를 요청 id · 채널로 나눠 줍니다.
     */
    class SW_GF_API EphemeralStoreRouter
    {
    public:
        using ReplyDelegate   = Delegate<void( const EphemeralReply& )>;
        using MessageDelegate = Delegate<void( const EphemeralMessage& )>;

        EphemeralStoreRouter();
        ~EphemeralStoreRouter();

        EphemeralStoreRouter( const EphemeralStoreRouter& )            = delete;
        EphemeralStoreRouter& operator=( const EphemeralStoreRouter& ) = delete;

        /** @brief @p pStore 는 빌려 쓴다(라우터보다 오래 산다). */
        void initialize( IEphemeralStore* pStore );
        /** @brief 기다리는 요청마다 `Unavailable` 답을 부르고 구독을 모두 풉니다. 두 번 불러도 됩니다. */
        void shutdown();

        /** @brief 앞의 답 · 메시지를 꺼내 델리게이트에 나눠 줍니다. 나눠 준 수입니다. */
        int32 pump();

        /** @brief 요청을 맡깁니다. @p onReply 가 비어 있으면 답을 버립니다(발행 · 지우기처럼 결과를 보지 않는 쓰기). 요청 id 입니다. */
        uint64 submit( const EphemeralRequest& request, ReplyDelegate onReply );
        /** @brief @p channel 을 구독합니다. 구독 id(0 = 실패 — 초기화 전)입니다. */
        uint64 subscribe( string_view channel, MessageDelegate onMessage );
        void   unsubscribe( uint64 subscriptionId );

        int32            getPendingCount() const { return static_cast<int32>( _mapRequestToReply.size() ); }
        IEphemeralStore* getStore() const { return _pStore; }

    private:
        struct Subscription
        {
            string          _channel{};
            MessageDelegate _onMessage{};
            uint64          _subscriptionId{ 0 };
        };

        bool hasChannelSubscriber( string_view channel ) const;

        vector<EphemeralReply>               _listReplyScratch;
        vector<EphemeralMessage>             _listMessageScratch;
        vector<MessageDelegate>              _listDispatchScratch;
        vector<Subscription>                 _listSubscription;
        unordered_map<uint64, ReplyDelegate> _mapRequestToReply;
        IEphemeralStore*                     _pStore;
        uint64                               _nextSubscriptionId;
    };
} // namespace sw
