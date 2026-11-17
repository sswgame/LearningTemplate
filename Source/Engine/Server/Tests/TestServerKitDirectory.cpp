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
