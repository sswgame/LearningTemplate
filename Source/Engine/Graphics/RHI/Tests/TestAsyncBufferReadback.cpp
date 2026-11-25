/**
 * @file TestAsyncBufferReadback.cpp
 * @brief 비동기 되읽기 고리(`AsyncBufferReadback`)가 GPU 완료 통지의 결과를 보고 읽는가 — `Abandoned` 는 읽지 않고 칸을 격리한다.
 * @details 가짜 디바이스(`test::FakeRHIDevice`)는 `notifyFrameCompletion` 을 쌓기만 한다. 테스트가 쌓인 통지를 불러 "GPU 가 끝냈다(또는 못 기다렸다)" 를 흉내 낸다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

/**
 * @brief [AsyncBufferReadbackTest] `Abandoned` 통지는 읽지 않고 칸을 쓰지 않게 두며, 뒤 요청의 `Completed` 가 그 칸을 다시 푼다
 * @details 변이: 통지 결과를 보지 않고 읽거나(옛 한 채널 해제 콜백) 칸을 먼저 비우면(`_arrBusy` 조기 해제) 읽기 수 · 요청 실패 단언이 진다.
 */
SW_TEST_CASE( AsyncBufferReadbackTest, AbandonedSlotIsNotReadAndStaysBlockedUntilALaterCompletion )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;
    test::FakeRHICommandList commands{ 0 };
    AsyncBufferReadback      readback;
    SW_REQUIRE_TRUE( readback.initialize( &device, 4, 1 ) );

    for ( uint64 tag = 1; tag <= AsyncBufferReadback::kSlotCount; ++tag )
    {
        SW_REQUIRE_TRUE( readback.request( commands, RHIBufferHandle{ 99 }, RHIBufferState::UnorderedAccess, tag ) );
    }
    SW_EXPECT_FALSE( readback.request( commands, RHIBufferHandle{ 99 }, RHIBufferState::UnorderedAccess, 100 ) ); // 칸이 모두 날아가는 중
    SW_REQUIRE_EQUAL( device._listFrameCompletion.size(), static_cast<size_t>( AsyncBufferReadback::kSlotCount ) );

    // 못 기다린 비우기: 읽지 않고, 칸도 풀지 않는다(GPU 복사 · 매핑이 아직 걸려 있을 수 있다).
    device._listFrameCompletion[0]( RHIFrameCompletion::Abandoned );
    SW_EXPECT_EQUAL( 0u, factory._readbackReadCount );
    SW_EXPECT_FALSE( readback.request( commands, RHIBufferHandle{ 99 }, RHIBufferState::UnorderedAccess, 101 ) );
    vector<uint8> bytes;
    uint64        tag{ 0 };
    SW_EXPECT_FALSE( readback.takeLatest( bytes, tag ) );

    // 뒤 요청이 끝났다 — 그 앞의 GPU 일도 끝났으니(큐 순서) 격리한 칸과 그 칸이 함께 풀린다.
    device._listFrameCompletion[1]( RHIFrameCompletion::Completed );
    SW_EXPECT_EQUAL( 1u, factory._readbackReadCount );
    SW_REQUIRE_TRUE( readback.takeLatest( bytes, tag ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 2 ), tag );
    SW_EXPECT_TRUE( readback.request( commands, RHIBufferHandle{ 99 }, RHIBufferState::UnorderedAccess, 102 ) );
    SW_EXPECT_TRUE( readback.request( commands, RHIBufferHandle{ 99 }, RHIBufferState::UnorderedAccess, 103 ) );
    SW_EXPECT_FALSE( readback.request( commands, RHIBufferHandle{ 99 }, RHIBufferState::UnorderedAccess, 104 ) );
    readback.shutdown();
}

/**
 * @brief [AsyncBufferReadbackTest] `Completed` 인데 읽지 못하면 칸을 비우지 않는다 — 매핑이 걸린 버퍼를 다음 복사의 목적지로 내주지 않는다
 * @details 변이: 읽기 전에 칸을 비우면(옛 `_arrBusy` 조기 해제) 셋째 요청이 성공해 진다.
 */
SW_TEST_CASE( AsyncBufferReadbackTest, FailedReadKeepsTheSlotOutOfUse )
{
    SW_EXPECT_LOG( "AsyncBufferReadback.FailedReadBackSlot" );
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;
    test::FakeRHICommandList commands{ 0 };
    AsyncBufferReadback      readback;
    SW_REQUIRE_TRUE( readback.initialize( &device, 4, 1 ) );
    for ( uint64 tag = 1; tag <= AsyncBufferReadback::kSlotCount; ++tag )
    {
        SW_REQUIRE_TRUE( readback.request( commands, RHIBufferHandle{ 99 }, RHIBufferState::UnorderedAccess, tag ) );
    }

    factory._bReadbackReadable = false;
    device._listFrameCompletion[0]( RHIFrameCompletion::Completed );
    SW_EXPECT_EQUAL( 1u, factory._readbackReadCount );
    SW_EXPECT_FALSE( readback.request( commands, RHIBufferHandle{ 99 }, RHIBufferState::UnorderedAccess, 10 ) );
    readback.shutdown();
}
