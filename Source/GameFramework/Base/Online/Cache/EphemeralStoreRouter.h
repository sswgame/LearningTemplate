/**
 * @file EphemeralStoreRouter.h
 * @brief 캐시 프런트(`IEphemeralStore`) 하나를 여러 서비스가 나눠 씁니다. 요청마다 답 델리게이트를, 채널마다 구독자 목록을 둡니다.
 *        프런트의 답과 메시지를 꺼내는 쪽은 이 라우터 하나뿐이어야 합니다.
 * @details - 서비스 스레드 하나에서 쓴다(`submit`, `subscribe`, 구독의 해제, `pump` 모두). 델리게이트는 `pump` 안에서 불린다.
 *          - 채널 구독은 `subscribe` 가 돌려준 구독(`Subscription`)이 소유한다. 구독이 사라지면 빠지고, 라우터가 먼저 내려가면(`shutdown`) 무효가 된다.
 *          - 델리게이트 안에서 `submit`, `subscribe`, 구독의 해제를 해도 된다(지금 나눠 주는 목록은 복사본이다).
 *          - 같은 채널을 둘이 구독하면 프런트에는 한 번만 구독하고, 마지막 구독자가 나갈 때 해제한다.
 *          - `shutdown` 은 답을 기다리는 요청마다 `Unavailable` 답을 한 번 부른다. 델리게이트는 언제나 정확히 한 번 불린다(`cancel` 한 것은 제외).
 *          - 델리게이트의 주인이 라우터보다 먼저 종료하면 기다리던 요청을 `cancel` 한다. 아니면 뒤의 `pump` 나 `shutdown` 이 사라진 객체를 부른다.
 *          `IEphemeralStore::pollReplies` 와 `pollMessages` 는 프런트 전체의 것을 꺼내므로, 서비스 둘이 직접 부르면 서로의 답을 가져간다.
 *          gRPC 와 Nakama 런타임처럼 런타임이 나눠 주는 구조다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class EphemeralStoreRouter
     * @brief 캐시 프런트의 답과 메시지를 요청 id 와 채널로 나눠 줍니다.
     */
    class SW_GF_API EphemeralStoreRouter : private SubscriptionSource
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
        /** @brief 보낸 요청의 델리게이트를 지웁니다. 답이 와도 버립니다(요청 자체는 캐시에서 이미 실행됐을 수 있다). 없거나 끝난 요청이면 아무것도 하지 않는다. */
        void cancel( uint64 requestID ) { _mapRequestToReply.erase( requestID ); }
        /**
         * @brief @p channel 을 구독하고 그 구독을 돌려줍니다. 구독이 사라지면 빠지고, 채널의 마지막 구독자면 앞의 구독도 닫습니다.
         * @details 초기화 전이거나 델리게이트가 비었으면 오류를 남기고 빈 구독을 돌려줍니다(예전에는 0 을 조용히 돌려줬다).
         */
        [[nodiscard]] Subscription subscribe( string_view channel, MessageDelegate onMessage );

        int32            getPendingCount() const { return static_cast<int32>( _mapRequestToReply.size() ); }
        IEphemeralStore* getStore() const { return _pStore; }

    private:
        struct ChannelSubscriber
        {
            string          _channel{};
            MessageDelegate _onMessage{};
            uint64          _subscriptionID{ 0 }; ///< 이 줄을 소유하는 구독의 번호
        };

        bool hasChannelSubscriber( string_view channel ) const;
        /** @brief 구독 @p id 가 풀렸습니다. 줄을 지우고 채널의 마지막 구독자였으면 앞의 구독을 닫습니다. */
        void releaseSubscription( uint64 id ) override;

        vector<EphemeralReply>               _listReplyScratch;
        vector<EphemeralMessage>             _listMessageScratch;
        vector<MessageDelegate>              _listDispatchScratch;
        vector<ChannelSubscriber>            _listSubscriber;
        unordered_map<uint64, ReplyDelegate> _mapRequestToReply;
        IEphemeralStore*                     _pStore;
        uint64                               _nextSubscriptionID;
    };
} // namespace sw
