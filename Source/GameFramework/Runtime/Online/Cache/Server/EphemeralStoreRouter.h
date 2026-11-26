/**
 * @file EphemeralStoreRouter.h
 * @brief 캐시 프런트(`IEphemeralStore`) 하나를 여러 서비스가 나눠 씁니다. 요청마다 완료(값을 잡은 람다)를, 채널마다 구독자 목록을 둡니다.
 *        프런트의 답과 메시지를 꺼내는 쪽은 이 라우터 하나뿐이어야 합니다.
 * @details - 서비스 스레드 하나에서 쓴다(`submit`, `subscribe`, 구독의 해제, `poll` 모두). 완료 · 델리게이트는 `poll` 안에서 불린다.
 *          - 답을 받는 요청은 번호를 돌려주지 않는다 — 기다리는 값은 완료가 값으로 잡는다. 서비스마다 `unordered_map< 요청 번호, 기다리는 값 >` 을 두면 맡기지 못한 요청(번호 0)이 표에 남아
 *            영영 끝나지 않거나, 내려갈 때 비우는 것을 빠뜨린다(`PendingRequestTable`).
 *          - 채널 구독은 `subscribe` 가 돌려준 구독(`Subscription`)이 소유한다. 구독이 사라지면 빠지고, 라우터가 먼저 내려가면(`shutdown`) 무효가 된다.
 *          - 델리게이트 안에서 `submit`, `subscribe`, 구독의 해제를 해도 된다(지금 나눠 주는 목록은 복사본이다).
 *          - 같은 채널을 둘이 구독하면 프런트에는 한 번만 구독하고, 마지막 구독자가 나갈 때 해제한다.
 *          - `shutdown` 은 답을 기다리는 요청마다 `Unavailable` 답을 한 번 부른다. 맡기지 못한 요청(죽은 주인 몫)도 그 자리에서 `Unavailable` 로 끝낸다 — 완료는 언제나 정확히 한 번 불린다(주인 몫을 놓은 것은 제외).
 *          - 답을 받는 요청은 주인 몫(`openReplyOwner` 의 구독)과 함께 맡긴다. 주인이 그 구독을 놓으면 맡긴 요청의 완료가 모두 지워진다 —
 *            주인이 라우터보다 먼저 내려가도 뒤의 `poll` 나 `shutdown` 이 사라진 객체를 부르지 않는다. 답을 보지 않는 쓰기는 주인 없이 맡긴다.
 *          `IEphemeralStore::pollReplies` 와 `pollMessages` 는 프런트 전체의 것을 꺼내므로, 서비스 둘이 직접 부르면 서로의 답을 가져간다.
 *          gRPC 와 Nakama 런타임처럼 런타임이 나눠 주는 구조다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStore.h"
#include "GameFramework/Runtime/Online/Service/PendingRequestTable.h"

namespace sw
{
    /**
     * @class EphemeralStoreRouter
     * @brief 캐시 프런트의 답과 메시지를 요청 id 와 채널로 나눠 줍니다.
     */
    class SW_GF_API EphemeralStoreRouter
    {
    public:
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
        int32 poll();

        /** @brief 답을 보지 않는 요청(발행 · 지우기처럼 결과를 보지 않는 쓰기)을 맡깁니다. 답은 버립니다. */
        void submit( const EphemeralRequest& request );
        /**
         * @brief 요청을 맡기고 답을 @p complete( const EphemeralReply& ) 로 받습니다. 기다리는 값은 완료가 값으로 잡습니다.
         * @details @p owner 는 이 라우터의 `openReplyOwner` 가 준 살아 있는 구독이어야 합니다. 아니면 오류를 남기고 맡기지 않으며 그 자리에서 `Unavailable` 로 끝냅니다.
         */
        template <typename TComplete>
        void submit( const EphemeralRequest& request, const Subscription& owner, TComplete&& complete )
        {
            uint64       ownerID   = 0;
            const uint64 requestID = submitOwned( request, owner, ownerID );
            if ( requestID == 0 )
            {
                complete( EphemeralReply::makeUnavailable() );
                return;
            }
            // 메모리 앞은 맡기는 시점에 답을 큐에 넣지만 꺼내는 것은 다음 poll 다 — 등록이 늦지 않다.
            const bool bTracked = _replies.track( requestID, ownerID, std::forward<TComplete>( complete ) );
            (void)bTracked;
            SW_ASSERT( bTracked ); // 앞이 낸 요청 번호는 겹치지 않는다
        }
        /**
         * @brief 답을 받는 주인의 몫을 엽니다. 주인이 멤버로 들고, 놓으면(소멸 · `reset`) 그 몫으로 맡긴 기다리는 요청의 완료를 모두 지웁니다.
         * @details 요청 자체는 캐시에서 이미 실행됐을 수 있다 — 답만 버린다. 라우터가 먼저 내려가면(`shutdown`) 몫은 무효가 된다.
         */
        [[nodiscard]] Subscription openReplyOwner() { return _subscriptions.issue( _subscriptionIDAllocator.allocate() ); }
        /**
         * @brief @p channel 을 구독하고 그 구독을 돌려줍니다. 구독이 사라지면 빠지고, 채널의 마지막 구독자면 앞의 구독도 닫습니다.
         * @details 초기화 전이거나 델리게이트가 비었으면 오류를 남기고 빈 구독을 돌려줍니다(예전에는 0 을 조용히 돌려줬다).
         */
        [[nodiscard]] Subscription subscribe( string_view channel, MessageDelegate onMessage );

        int32 getPendingCount() const { return _replies.getOpenCount(); }
        /** @brief 주인 몫 @p owner 로 맡겨 답을 기다리는 요청 수입니다. */
        int32            getPendingCount( const Subscription& owner ) const { return _replies.getOpenCountOf( _subscriptions.findIssued( owner ) ); }
        IEphemeralStore* getStore() const { return _pStore; }

    private:
        struct ChannelSubscriber
        {
            string          _channel{};
            MessageDelegate _onMessage{};
            uint64          _subscriptionID{ 0 }; ///< 이 줄을 소유하는 구독의 번호
        };
        /** @brief 살아 있는 몫이면 앞에 맡기고 요청 번호를, 아니면 오류를 남기고 0 을 돌려줍니다. */
        uint64 submitOwned( const EphemeralRequest& request, const Subscription& owner, uint64& outOwnerID );
        bool   hasChannelSubscriber( string_view channel ) const;
        /** @brief 구독 @p id 가 풀렸습니다. 채널 구독이면 줄을 지우고 채널의 마지막 구독자였으면 앞의 구독을 닫습니다. 주인 몫이면 그 몫의 기다리는 답을 지웁니다. */
        void releaseSubscription( uint64 id );

    private:
        vector<EphemeralReply>                      _listReplyScratch;
        vector<EphemeralMessage>                    _listMessageScratch;
        vector<MessageDelegate>                     _listDispatchScratch;
        vector<ChannelSubscriber>                   _listSubscriber;
        PendingRequestTable<uint64, EphemeralReply> _replies; ///< 답을 기다리는 요청(주인 몫의 번호가 주인이다)
        IEphemeralStore*                            _pStore;
        IDAllocator<uint64>                         _subscriptionIDAllocator;
        /** @brief 이 객체가 내준 구독입니다(믹스인 상속 대신 합성). 풀리면 `releaseSubscription` 이 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        SubscriptionList _subscriptions{ SW_DELEGATE_METHOD( SubscriptionList::ReleaseFunction, &EphemeralStoreRouter::releaseSubscription, this ) };
    };
} // namespace sw
