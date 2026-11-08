#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIBufferTransitionBatch.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

/** @brief [RHIBufferTransitionBatchTest] 같은 버퍼는 하나로 합쳐 나중 상태가 이기고, 핸들 0 은 넣지 않습니다. */
SW_TEST_CASE( RHIBufferTransitionBatchTest, MergesSameBufferAndSkipsNull )
{
    RHIBufferTransitionBatch batch;
    batch.add( 7, RHIBufferState::UnorderedAccess );
    batch.add( 0, RHIBufferState::UnorderedAccess );
    batch.add( 9, RHIBufferState::UnorderedAccess );
    batch.add( 7, RHIBufferState::IndirectArgument );

    const vector<RHIBufferTransition>& listPending = batch.getPending();
    SW_REQUIRE_EQUAL( listPending.size(), static_cast<size_t>( 2 ) );
    SW_EXPECT_EQUAL( listPending[0]._buffer, static_cast<RHIBufferHandle>( 7 ) );
    SW_EXPECT_TRUE( listPending[0]._state == RHIBufferState::IndirectArgument );
    SW_EXPECT_EQUAL( listPending[1]._buffer, static_cast<RHIBufferHandle>( 9 ) );
}

/** @brief [RHIBufferTransitionBatchTest] 한 단계의 전이는 `transitionBuffers` 한 번으로 나가고, 빈 묶음은 아무것도 내지 않습니다. */
SW_TEST_CASE( RHIBufferTransitionBatchTest, FlushIssuesOneCall )
{
    test::FakeRHICommandList commands{ 0 };
    RHIBufferTransitionBatch batch;
    batch.flush( commands );
    SW_EXPECT_EQUAL( commands._transitionBatchCount, 0u );

    batch.add( 1, RHIBufferState::UnorderedAccess );
    batch.add( 2, RHIBufferState::UnorderedAccess );
    batch.add( 3, RHIBufferState::UnorderedAccess );
    batch.flush( commands );
    SW_EXPECT_EQUAL( commands._transitionBatchCount, 1u );
    SW_EXPECT_EQUAL( commands._transitionCount, 3u );
    SW_EXPECT_TRUE( batch.getPending().empty() );
}
