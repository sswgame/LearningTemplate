// MaterialChangeLogTest — 머티리얼 변경 기록: 읽는 쪽은 지난 세대 뒤에 바뀐 주인만 받고, 넘침 · 주인을 모르는 변경이면 "전부 다시 본다" 로 답한다(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/Material/MaterialChangeLog.h"

using namespace sw;

/**
 * @brief [MaterialChangeLogTest] 바뀐 주인만 중복 없이 돌려주고, 이미 읽은 세대는 다시 주지 않는다
 * @details 장면 빌더가 이 목록의 원소만 다시 맞춘다 — 같은 머티리얼을 한 프레임에 여러 번 바꿔도 한 번만 본다.
 */
SW_TEST_CASE( MaterialChangeLogTest, CollectsOnlyOwnersChangedSinceGeneration )
{
    MaterialChangeLog log;
    int32             ownerA = 0;
    int32             ownerB = 0;
    int32             ownerC = 0;
    const uint64      start  = log.getGeneration();
    log.record( &ownerA );
    log.record( &ownerB );
    log.record( &ownerA );
    const uint64 middle = log.getGeneration();
    log.record( &ownerC );
    const uint64 end = log.getGeneration();
    SW_EXPECT_EQUAL( start + 4u, end );

    vector<const void*> listOwner;
    SW_REQUIRE_TRUE( log.collectChangedOwners( start, middle, listOwner ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), listOwner.size() );
    SW_EXPECT_FALSE( VectorUtil::contains( listOwner, static_cast<const void*>( &ownerC ) ) );

    SW_REQUIRE_TRUE( log.collectChangedOwners( middle, end, listOwner ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listOwner.size() );
    SW_EXPECT_TRUE( listOwner[0] == static_cast<const void*>( &ownerC ) );

    SW_REQUIRE_TRUE( log.collectChangedOwners( end, end, listOwner ) );
    SW_EXPECT_TRUE( listOwner.empty() );
}

/**
 * @brief [MaterialChangeLogTest] 주인을 모르는 변경(묶음 값)이 끼거나 고리가 넘치면 false — 읽는 쪽은 모든 원소를 다시 맞춘다
 * @details 음성 대조: 같은 구간에서 모르는 변경을 빼면 true 다(넘침 판정이 늘 false 를 내는 것이 아니다).
 */
SW_TEST_CASE( MaterialChangeLogTest, UnknownOwnerOrOverflowAsksForFullRefresh )
{
    MaterialChangeLog   log;
    int32               owner = 0;
    vector<const void*> listOwner;

    const uint64 beforeUnknown = log.getGeneration();
    log.record( &owner );
    log.record( nullptr );
    SW_EXPECT_FALSE( log.collectChangedOwners( beforeUnknown, log.getGeneration(), listOwner ) );
    SW_EXPECT_TRUE( log.collectChangedOwners( beforeUnknown, beforeUnknown + 1u, listOwner ) ); // 모르는 변경 앞까지는 주인을 안다

    const uint64 beforeOverflow = log.getGeneration();
    for ( uint32 index = 0; index < MaterialChangeLog::kCapacity + 1u; ++index )
        log.record( &owner );
    SW_EXPECT_FALSE( log.collectChangedOwners( beforeOverflow, log.getGeneration(), listOwner ) );
    SW_EXPECT_TRUE( log.collectChangedOwners( log.getGeneration() - 1u, log.getGeneration(), listOwner ) );
}

namespace
{
    /** @brief 지켜보기 테스트의 가짜 주인입니다 — 늦은 일이 끝났는지만 든다(덮어쓴 텍스처 로드 자리). */
    struct CompletionTestOwner
    {
        bool _bDone{ false };

        static MaterialCompletionWatch::PollResult poll( void* pOwner )
        {
            return static_cast<const CompletionTestOwner*>( pOwner )->_bDone ? MaterialCompletionWatch::PollResult::Complete : MaterialCompletionWatch::PollResult::Pending;
        }
    };
} // namespace

/**
 * @brief [MaterialChangeLogTest] 늦게 끝나는 일(덮어쓴 텍스처 로드)은 끝나는 프레임에 주인이 변경 기록에 실리고 지켜보기에서 빠진다
 * @details 음성 대조: 아직 로드 중이면 기록에 아무것도 오르지 않는다 — 끝남을 기다리지 않고 미리 적거나, 끝나도 적지 않으면 둘 중 하나가 깨진다.
 *          장면 빌더는 바뀐 주인의 원소만 다시 맞추므로, 이 기록이 없으면 실패한 덮어쓰기가 그 인스턴스가 다시 바뀔 때까지 부모 텍스처로 되돌아가지 않는다.
 */
SW_TEST_CASE( MaterialChangeLogTest, CompletedLoadRecordsItsOwnerOnce )
{
    MaterialChangeLog       log;
    MaterialCompletionWatch watch;
    CompletionTestOwner     owner;
    CompletionTestOwner     other;
    watch.watch( &owner, &CompletionTestOwner::poll );
    watch.watch( &owner, &CompletionTestOwner::poll ); // 같은 주인은 한 번만 지켜본다
    watch.watch( &other, &CompletionTestOwner::poll );
    SW_EXPECT_EQUAL( 2u, watch.getWatchCount() );

    const uint64 start = log.getGeneration();
    SW_EXPECT_EQUAL( 0u, watch.pollAll( log ) );
    SW_EXPECT_EQUAL( start, log.getGeneration() ); // 음성 대조 — 로드 중이면 적지 않는다

    owner._bDone = true;
    SW_EXPECT_EQUAL( 1u, watch.pollAll( log ) );
    vector<const void*> listOwner;
    SW_REQUIRE_TRUE( log.collectChangedOwners( start, log.getGeneration(), listOwner ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listOwner.size() );
    SW_EXPECT_TRUE( listOwner[0] == static_cast<const void*>( &owner ) );
    SW_EXPECT_EQUAL( 1u, watch.getWatchCount() );

    // 끝난 주인은 다시 적지 않고, 사라지는 주인(소멸자)은 끝나기 전에 빠진다.
    const uint64 afterComplete = log.getGeneration();
    watch.unwatch( &other );
    other._bDone = true;
    SW_EXPECT_EQUAL( 0u, watch.pollAll( log ) );
    SW_EXPECT_EQUAL( afterComplete, log.getGeneration() );
    SW_EXPECT_EQUAL( 0u, watch.getWatchCount() );
}
