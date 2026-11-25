#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

// 버퍼 구간 복사(R-15, 정점 풀이 GPU 원본에서 채운다): 명령 리스트 계약의 인자가 그대로 기록된다(가짜 리스트가 받은 순서대로 적는다).

using namespace sw;

/** @brief [RHIBufferCopyTest] copyBufferRegion 은 도착 · 원본 · 오프셋 · 크기를 그대로 기록한다. 변이: 인자 순서를 바꾸면(원본 ↔ 도착) 단언이 진다. */
SW_TEST_CASE( RHIBufferCopyTest, CopyRegionRecordsArguments )
{
    test::FakeRHICommandList list{ 0 };
    list.copyBufferRegion( RHIBufferHandle{ 7 }, 1200, RHIBufferHandle{ 3 }, 48, 960 );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), list._listBufferCopy.size() );
    const test::FakeRHICommandList::BufferCopy& copy = list._listBufferCopy[0];
    SW_EXPECT_EQUAL( static_cast<RHIBufferHandle>( 7 ), copy._destination );
    SW_EXPECT_EQUAL( uint64{ 1200 }, copy._destinationOffset );
    SW_EXPECT_EQUAL( static_cast<RHIBufferHandle>( 3 ), copy._source );
    SW_EXPECT_EQUAL( uint64{ 48 }, copy._sourceOffset );
    SW_EXPECT_EQUAL( uint64{ 960 }, copy._sizeBytes );
}

/**
 * @brief [RHIBufferCopyTest] 버퍼 끝을 넘는 구간 복사는 어느 백엔드에도 닿지 않고 알린다 — 끝에 딱 맞는 복사는 나간다
 * @details 변이: 검사가 크기만 보고 오프셋을 빼면 둘째 · 셋째 복사가 백엔드에 닿아 진다.
 */
SW_TEST_CASE( RHIBufferCopyTest, CopyOutsideABufferIsRejected )
{
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    SW_EXPECT_LOG( "RHI.CopyBufferRegionOutside" );
    context.copyBufferRegion( RHIBufferHandle{ 2 }, 192, RHIBufferHandle{ 1 }, 0, 64 );
    SW_EXPECT_EQUAL( 1u, context._copyRegionCount );
    context.copyBufferRegion( RHIBufferHandle{ 2 }, 196, RHIBufferHandle{ 1 }, 0, 64 ); // 도착이 4 바이트 넘친다
    context.copyBufferRegion( RHIBufferHandle{ 2 }, 0, RHIBufferHandle{ 1 }, 256, 4 );  // 원본 끝에서 읽는다
    SW_EXPECT_EQUAL( 1u, context._copyRegionCount );
}
