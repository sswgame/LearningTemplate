/**
 * @file TestDrawSnapshotHandoff.cpp
 * @brief UI 스레드와 렌더 스레드가 draw 스냅샷 칸을 주고받는 약속(`DrawSnapshotHandoff`) 테스트입니다. 한 스레드에서 두 쪽의 호출 순서를 그대로 밟습니다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Backend/DrawSnapshotHandoff.h"

using sw::editor::DrawSnapshotHandoff;

/**
 * @brief [DrawSnapshotHandoffTest] 렌더 스레드가 앞 스냅샷을 그리는 동안 UI 스레드는 다른 칸에 쓰고 낸다 — 그리는 칸은 내주지 않고, 낸 스냅샷은 건너뛰지 않는다
 */
SW_TEST_CASE( DrawSnapshotHandoffTest, UIWritesTheNextSlotWhileTheRenderThreadDrawsThePublishedOne )
{
    DrawSnapshotHandoff handoff;
    SW_EXPECT_TRUE( handoff.isIdle() );

    const uint32 first = handoff.waitForWriteSlot();
    handoff.publish( first );
    SW_EXPECT_FALSE( handoff.isIdle() ); // 낸 것을 렌더 스레드가 아직 집지 않았다

    SW_EXPECT_EQUAL( first, handoff.beginRender() );
    const uint32 second = handoff.waitForWriteSlot(); // 그리는 동안 받는다 — 기다리지 않는다
    SW_EXPECT_NOT_EQUAL( first, second );
    handoff.publish( second ); // 앞 것은 이미 집혔다 — 기다리지 않는다
    handoff.endRender();
    SW_EXPECT_FALSE( handoff.isIdle() ); // 두 번째를 아직 그리지 않았다 — 그린 프레임의 끝은 낸 것을 건너뛰지 않는다

    SW_EXPECT_EQUAL( second, handoff.beginRender() );
    handoff.endRender();
    SW_EXPECT_TRUE( handoff.isIdle() );

    SW_EXPECT_EQUAL( second, handoff.beginRender() ); // 새로 낸 것이 없으면 같은 칸을 다시 그린다
    SW_EXPECT_NOT_EQUAL( second, handoff.waitForWriteSlot() );
    handoff.endRender();
}

/**
 * @brief [DrawSnapshotHandoffTest] present 훅까지 가지 못한 프레임의 끝은 낸 스냅샷을 건너뛴 것으로 친다 — UI 스레드가 게시에서 멈추지 않는다
 * @details 렌더 스레드는 프레임 본문이 멈춰도 postPresent(`endRender`)는 부른다. 게시 대기를 풀지 않으면 UI 스레드는 다음 게시에서 기다리느라 패킷을 내지 못하고
 *          렌더 스레드는 그 패킷을 기다린다.
 */
SW_TEST_CASE( DrawSnapshotHandoffTest, AFrameThatSkippedTheUIReleasesThePublishedSnapshot )
{
    DrawSnapshotHandoff handoff;
    handoff.publish( handoff.waitForWriteSlot() );
    handoff.endRender(); // beginRender 없이
    SW_EXPECT_TRUE( handoff.isIdle() );
    handoff.publish( handoff.waitForWriteSlot() ); // 기다리지 않는다
    SW_EXPECT_FALSE( handoff.isIdle() );
}

/**
 * @brief [DrawSnapshotHandoffTest] 포기는 그리는 칸과 게시 대기를 함께 푼다(에디터 인스턴스를 없앨 때 — 그 프레임을 그릴 쪽이 없다)
 */
SW_TEST_CASE( DrawSnapshotHandoffTest, AbandonReleasesTheRenderingSlotAndThePendingSnapshot )
{
    DrawSnapshotHandoff handoff;
    handoff.publish( handoff.waitForWriteSlot() );
    (void)handoff.beginRender();
    handoff.publish( handoff.waitForWriteSlot() );
    handoff.abandon();
    SW_EXPECT_TRUE( handoff.isIdle() );
}
