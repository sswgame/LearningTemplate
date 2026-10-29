// RegistrationList — 구독으로 올리기(`addSubscribed`): 구독이 풀리면 주인의 해제 함수가 그 항목으로 불리고, 거절은 빈 구독이며, 목록이 먼저 사라지면 구독이 무효가 된다.
#include "pch.h"

#include "Core/String/RegistrationList.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    struct SubscribedItem
    {
        int32 _value{ 0 };
    };

    /** @brief 해제 함수에서 빼고 뒷정리 횟수를 세는 주인입니다(레지스트리가 하는 일과 같은 모양). */
    class SubscribedOwner
    {
    public:
        void releaseItem( SubscribedItem* pItem )
        {
            if ( _list.remove( pItem ) )
                ++_releaseCount;
        }

        RegistrationList<SubscribedItem> _list{ "Test.SubscribedItem", RegistrationList<SubscribedItem>::ReleaseFunction::create<&SubscribedOwner::releaseItem>( this ) };
        int32                            _releaseCount{ 0 };
    };
} // namespace

SW_TEST_CASE( RegistrationListTest, SubscriptionReleaseCallsTheOwnerWithTheItem )
{
    SubscribedOwner owner;
    SubscribedItem  first;
    SubscribedItem  second;
    Subscription    firstSubscription  = owner._list.addSubscribed( &first, "Test item", "First" );
    Subscription    secondSubscription = owner._list.addSubscribed( &second, "Test item", "Second" );
    SW_EXPECT_TRUE( firstSubscription.isActive() );
    SW_EXPECT_EQUAL( 2u, owner._list.getCount() );
    firstSubscription.reset();
    SW_EXPECT_EQUAL( 1, owner._releaseCount );
    SW_EXPECT_FALSE( owner._list.contains( &first ) );
    SW_EXPECT_TRUE( owner._list.getAt( 0 ) == &second ); // 남은 것의 순서는 그대로
}

SW_TEST_CASE( RegistrationListTest, RejectedSubscribedAddIsAnEmptySubscription )
{
    SubscribedOwner    owner;
    SubscribedItem     item;
    const Subscription subscription = owner._list.addSubscribed( &item, "Test item" );
    const Subscription again        = owner._list.addSubscribed( &item, "Test item" ); // 이미 올라 있다 — 먼저 받은 구독이 쥔다
    SW_EXPECT_TRUE( subscription.isActive() );
    SW_EXPECT_FALSE( again.isActive() );
    SW_EXPECT_EQUAL( 1u, owner._list.getCount() );
}

SW_TEST_CASE( RegistrationListTest, ClearInvalidatesSubscriptionsWithoutCallingTheOwner )
{
    SubscribedOwner owner;
    SubscribedItem  item;
    Subscription    subscription = owner._list.addSubscribed( &item, "Test item" );
    owner._list.clear();
    SW_EXPECT_FALSE( subscription.isActive() );
    subscription.reset();
    SW_EXPECT_EQUAL( 0, owner._releaseCount );
}
