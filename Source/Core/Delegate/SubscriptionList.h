/**
 * @file Core/Delegate/SubscriptionList.h
 * @brief 구독을 내주는 쪽이 **멤버로 드는** 구독 목록입니다 — 믹스인 상속(`private SubscriptionSource`) 대신 합성으로 씁니다.
 * @details 번호(`IDAllocator`)로 찾는 값 있는 등록(명령 처리기 · 받은 편지함 · 주제 구독)에 쓴다. 주인은 등록할 때 `_subscriptions.issue( id )` 로 구독을 돌려주고, 구독이 풀리면 생성자로 받은 해제 델리게이트(`releaseSubscription( id )`)가 불린다.
 *          해제 델리게이트가 주인 주소를 들므로 주인은 복사 · 이동하지 않는 객체여야 한다(서브시스템 · 레지스트리). 출처 자체가 값으로 오가는
 *          것(`MulticastDelegate` · `DelegateSlot`)은 `SubscriptionSource` 를 그대로 상속한다 — 이동 생성자가 구독을 새 자리로 데려간다.
 *
 *          class UIScreen
 *          {
 *              void releaseSubscription( uint64 id );
 *              SubscriptionList _subscriptions{ SubscriptionList::ReleaseFunction::create<&UIScreen::releaseSubscription>( this ) };
 *          };
 *
 *          등록 하나가 항목 주소 하나인 주인(컴포넌트 · 처리기를 빌려 드는 레지스트리)은 `ItemSubscriptionList<T>` 를 쓴다 — 해제 함수가 그 형식의 포인터를
 *          받으므로 주인이 id 를 주소로 되돌리거나, 종류 여럿을 한 목록에 섞어 id 에 종류 비트를 싣지 않는다(종류마다 목록 하나).
 *          항목을 `RegistrationList` 에 두는 주인은 `RegistrationList::addSubscribed` 가 이 목록을 안에 들고 대신 낸다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"

namespace sw
{
    /**
     * @class SubscriptionList
     * @brief 살아 있는 구독을 들고, 풀린 구독을 주인의 해제 델리게이트로 알리는 목록입니다. 사라지면 남은 구독을 모두 무효화합니다.
     */
    class SubscriptionList final : public SubscriptionSource
    {
    public:
        /** @brief 구독 id 가 풀렸을 때 부르는 주인의 함수입니다. 목록 잠금은 풀려 있다. */
        using ReleaseFunction = Delegate<void( uint64 )>;

        explicit SubscriptionList( const ReleaseFunction& onRelease ) noexcept
            : _onRelease{ onRelease }
        {
        }
        ~SubscriptionList() override = default;

        SubscriptionList( const SubscriptionList& )            = delete;
        SubscriptionList& operator=( const SubscriptionList& ) = delete;
        SubscriptionList( SubscriptionList&& )                 = delete;
        SubscriptionList& operator=( SubscriptionList&& )      = delete;

        /** @brief 등록 @p id 를 소유하는 구독을 만듭니다. */
        [[nodiscard]] Subscription issue( const uint64 id ) noexcept { return issueSubscription( id ); }
        /** @brief @p subscription 이 이 목록이 내준 살아 있는 구독이면 그 id, 아니면 0 입니다. */
        [[nodiscard]] uint64 findIssued( const Subscription& subscription ) const noexcept { return findIssuedID( subscription ); }
        /** @brief 살아 있는 구독을 모두 무효화합니다(해제 델리게이트는 부르지 않는다 — 주인이 등록을 따로 비운다). */
        void detachAll() noexcept { detachAllSubscriptions(); }

    private:
        void releaseSubscription( const uint64 id ) override
        {
            if ( _onRelease.isBound() )
                _onRelease( id );
        }

        ReleaseFunction _onRelease;
    };

    /**
     * @class ItemSubscriptionList
     * @brief 등록 하나가 항목(@p T) 주소 하나인 구독 목록입니다. 풀리면 주인의 해제 함수가 그 항목 포인터로 불립니다.
     * @details 주인은 해제 함수에서 자기 잠금을 잡고 항목을 빼고 뒷정리를 한다(목록은 잠금을 들지 않는다 — 주인 잠금이 "가장 안쪽" 이라는 규칙을 그대로 둔다).
     *          해제 함수는 항목을 역참조하지 않고 주소로만 찾는다 — 항목이 소멸 중에 구독을 놓는다. 사라지면 남은 구독을 모두 무효화합니다.
     */
    template <typename T>
    class ItemSubscriptionList final : public SubscriptionSource
    {
    public:
        /** @brief 항목 @p pItem 의 구독이 풀렸을 때 부르는 주인의 함수입니다. 목록 잠금은 풀려 있다. */
        using ReleaseFunction = Delegate<void( T* )>;

        explicit ItemSubscriptionList( const ReleaseFunction& onRelease ) noexcept
            : _onRelease{ onRelease }
        {
        }
        ~ItemSubscriptionList() override = default;

        ItemSubscriptionList( const ItemSubscriptionList& )            = delete;
        ItemSubscriptionList& operator=( const ItemSubscriptionList& ) = delete;
        ItemSubscriptionList( ItemSubscriptionList&& )                 = delete;
        ItemSubscriptionList& operator=( ItemSubscriptionList&& )      = delete;

        /** @brief 항목 @p pItem 의 등록을 소유하는 구독을 만듭니다. 해제 함수 없이 내주지 않는다(풀릴 때 부를 곳이 없다). */
        [[nodiscard]] Subscription issue( T* pItem ) noexcept
        {
            SW_ASSERT( _onRelease.isBound() );
            return issueSubscription( static_cast<uint64>( reinterpret_cast<uintptr_t>( pItem ) ) );
        }
        /** @brief 살아 있는 구독을 모두 무효화합니다(해제 함수는 부르지 않는다 — 주인이 등록을 따로 비운다). */
        void detachAll() noexcept { detachAllSubscriptions(); }

    private:
        void releaseSubscription( const uint64 id ) override { _onRelease( reinterpret_cast<T*>( static_cast<uintptr_t>( id ) ) ); }

        ReleaseFunction _onRelease;
    };
} // namespace sw
