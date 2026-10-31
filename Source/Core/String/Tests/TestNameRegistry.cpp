// NameRegistry — 빈 · 겹친 이름 거절과 이름으로 빼기의 순서, 주인 있는 등록(`addOwned`): 덮으면 앞 값이 그림자로 남고, 지금 주인이 풀리면 그림자로 돌아가며, 그림자 주인이 먼저 풀리면 그 그림자만 버린다.
#include "pch.h"

#include "Core/String/RegistrationList.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

SW_TEST_CASE( NameRegistryTest, OwnedValueFallsBackToTheValueItCovered )
{
    NameRegistry<int32> registry;
    SW_EXPECT_TRUE( registry.add( hashed_string( "Percent" ), 1 ) ); // 주인 없는 기본값
    const uint32 revisionBefore = registry.getRevision();
    {
        const Subscription gameOwned = registry.addOwned( hashed_string( "Percent" ), 2, "Test value" );
        SW_EXPECT_EQUAL( 2, *registry.find( hashed_string( "Percent" ) ) );
        SW_EXPECT_TRUE( registry.getRevision() != revisionBefore );
    }
    SW_EXPECT_EQUAL( 1, *registry.find( hashed_string( "Percent" ) ) ); // 덮었던 기본으로 돌아왔다
    SW_EXPECT_EQUAL( 1u, registry.getCount() );
}

SW_TEST_CASE( NameRegistryTest, ReplacedOwnerReleasingLaterKeepsTheNewValue )
{
    NameRegistry<int32> registry;
    Subscription        oldOwner = registry.addOwned( hashed_string( "QuitGame" ), 10, "Test value" );
    Subscription        newOwner = registry.addOwned( hashed_string( "QuitGame" ), 20, "Test value" ); // 핫 리로드의 새 인스턴스가 먼저 건다
    oldOwner.reset();                                                                                  // 옛 인스턴스가 내려간다 — 그림자만 버린다
    SW_EXPECT_EQUAL( 20, *registry.find( hashed_string( "QuitGame" ) ) );
    newOwner.reset();
    SW_EXPECT_TRUE( registry.find( hashed_string( "QuitGame" ) ) == nullptr ); // 덮은 것이 없던 이름은 거둔다
    SW_EXPECT_EQUAL( 0u, registry.getCount() );
}

SW_TEST_CASE( NameRegistryTest, NewOwnerReleasingFirstRestoresTheOldOwner )
{
    NameRegistry<int32> registry;
    Subscription        oldOwner = registry.addOwned( hashed_string( "Seconds" ), 10, "Test value" );
    {
        const Subscription newOwner = registry.addOwned( hashed_string( "Seconds" ), 20, "Test value" );
    }
    SW_EXPECT_EQUAL( 10, *registry.find( hashed_string( "Seconds" ) ) );
    oldOwner.reset(); // 돌아온 값의 주인이 다시 거둔다
    SW_EXPECT_TRUE( registry.find( hashed_string( "Seconds" ) ) == nullptr );
}

SW_TEST_CASE( NameRegistryTest, EmptyNameIsRejectedWithAnEmptySubscription )
{
    SW_EXPECT_LOG( "Registration.EmptyName" );
    NameRegistry<int32> registry;
    const Subscription  subscription = registry.addOwned( hashed_string(), 1, "Test value" );
    SW_EXPECT_FALSE( subscription.isActive() );
    SW_EXPECT_EQUAL( 0u, registry.getCount() );
}

/**
 * @brief [NameRegistryTest] 이름이 비었거나 겹치면 올리지 않고, 이름으로 빼도 남은 것의 등록 순서는 그대로다
 * @details 값을 소유하는 레지스트리(활동 종류 · 연산 · 모듈 렌더 패스)가 이름 거절과 빼기를 직접 짜지 않게 하는 공용 형식이다.
 */
SW_TEST_CASE( NameRegistryTest, RejectsEmptyAndDuplicateNamesAndRemovesByNameInOrder )
{
    NameRegistry<int32> registry;
    SW_EXPECT_FALSE( registry.add( hashed_string{}, 1 ) );
    SW_EXPECT_TRUE( registry.add( hashed_string{ "a" }, 1 ) );
    SW_EXPECT_TRUE( registry.add( hashed_string{ "b" }, 2 ) );
    SW_EXPECT_TRUE( registry.add( hashed_string{ "c" }, 3 ) );
    SW_EXPECT_FALSE( registry.add( hashed_string{ "b" }, 9 ) );
    SW_REQUIRE_EQUAL( 3u, registry.getCount() );
    SW_REQUIRE_NOT_NULL( registry.find( hashed_string{ "b" } ) );
    SW_EXPECT_EQUAL( 2, *registry.find( hashed_string{ "b" } ) );

    SW_EXPECT_TRUE( registry.remove( hashed_string{ "b" } ) );
    SW_EXPECT_FALSE( registry.remove( hashed_string{ "b" } ) );
    SW_EXPECT_FALSE( registry.remove( hashed_string{} ) );
    SW_REQUIRE_EQUAL( 2u, registry.getCount() );
    SW_EXPECT_NULL( registry.find( hashed_string{ "b" } ) );
    SW_EXPECT_EQUAL( 1, registry.getItems()[0] );
    SW_EXPECT_EQUAL( 3, registry.getItems()[1] );
    SW_EXPECT_TRUE( registry.add( hashed_string{ "b" }, 4 ) ); // 빼면 같은 이름을 다시 올릴 수 있다
}
