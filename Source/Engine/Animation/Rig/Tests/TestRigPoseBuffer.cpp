// RigPoseBufferTest — 부모 표로 조상을 묻는다: 자기 · 부모 · 조부모는 조상이고, 형제 가지 · 자손 · 없는 본(-1)은 아니다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Skeletal/Pose.h"

/**
 * @brief [RigPoseBufferTest] `isAncestorOf` 는 부모 사슬을 뿌리까지 따라간다 — 자기도 조상으로 치고, 다른 가지와 자손은 아니다
 * @details 본 넷: 0(뿌리) → 1 → 2, 0 → 3. 변이: 사슬을 한 단계만 보면 조부모(0 → 2) 단언이 진다.
 */
SW_TEST_CASE( RigPoseBufferTest, AncestorFollowsTheParentChain )
{
    sw::Pose pose;
    pose.resize( 4 );
    sw::RigPoseBuffer buffer;
    buffer.initialize( pose, { -1, 0, 1, 0 } );
    SW_REQUIRE_EQUAL( 4u, buffer.getBoneCount() );

    SW_EXPECT_TRUE( buffer.isAncestorOf( 2, 2 ) ); // 자기
    SW_EXPECT_TRUE( buffer.isAncestorOf( 1, 2 ) ); // 부모
    SW_EXPECT_TRUE( buffer.isAncestorOf( 0, 2 ) ); // 조부모
    SW_EXPECT_TRUE( buffer.isAncestorOf( 0, 3 ) );
    SW_EXPECT_FALSE( buffer.isAncestorOf( 2, 1 ) );  // 자손은 조상이 아니다
    SW_EXPECT_FALSE( buffer.isAncestorOf( 3, 2 ) );  // 다른 가지
    SW_EXPECT_FALSE( buffer.isAncestorOf( -1, 2 ) ); // 없는 본
}
