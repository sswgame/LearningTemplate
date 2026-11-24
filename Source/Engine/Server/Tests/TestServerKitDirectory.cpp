#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Server/ServerKitDirectory.h"

// 서버 키트 창구 — 서버 서비스가 선 동안만 키트 범위를 보이고, 내려가면(리로드 · stopAll) 비어 게임이 매달린 범위를 보지 않는다.

using namespace sw;

/** @brief [ServerKitDirectoryTest] 공개한 범위만 보이고, 거두면 비며, 다른 범위의 거두기는 무시한다 */
SW_TEST_CASE( ServerKitDirectoryTest, PublishedScopeIsVisibleUntilWithdrawn )
{
    ServerKitDirectory directory;
    SW_EXPECT_TRUE( directory.findKitScope() == nullptr ); // 온라인 조립이 서기 전
    SubsystemScope kits;
    SubsystemScope other;
    directory.publish( &kits );
    SW_EXPECT_TRUE( directory.findKitScope() == &kits );
    directory.withdraw( &other );
    SW_EXPECT_TRUE( directory.findKitScope() == &kits );
    directory.withdraw( &kits ); // 서버 서비스가 내려간다
    SW_EXPECT_TRUE( directory.findKitScope() == nullptr );
    directory.publish( &kits ); // 리로드 뒤 다시 선다
    SW_EXPECT_TRUE( directory.findKitScope() == &kits );
}

/** @brief [ServerKitDirectoryTest] 거두기 직전에 알리고, 그 안에서는 범위를 아직 찾을 수 있다 — 게임 서버 코드가 마지막 저장을 맡기는 자리다 */
SW_TEST_CASE( ServerKitDirectoryTest, WithdrawIsAnnouncedWhileTheScopeIsVisible )
{
    ServerKitDirectory       directory;
    SubsystemScope           kits;
    SubsystemScope           other;
    uint32                   announcedCount = 0;
    const ISubsystemLocator* pSeen          = nullptr;
    const Subscription       beforeWithdraw = directory.subscribeBeforeWithdraw( SW_DELEGATE_LAMBDA( Delegate<void()>, [&announcedCount, &pSeen, &directory]()
          {
        ++announcedCount;
        pSeen = directory.findKitScope();
    } ) );
    directory.publish( &kits );
    directory.withdraw( &other ); // 공개한 범위가 아니다
    SW_EXPECT_EQUAL( 0u, announcedCount );
    directory.withdraw( &kits );
    SW_EXPECT_EQUAL( 1u, announcedCount );
    SW_EXPECT_TRUE( pSeen == &kits );
    SW_EXPECT_TRUE( directory.findKitScope() == nullptr );
}
