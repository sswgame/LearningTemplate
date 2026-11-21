#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

// 버퍼 구간 복사(R-15 — 정점 풀이 GPU 원본에서 채운다) — 명령 리스트 계약의 인자가 그대로 기록된다(가짜 리스트가 받은 순서대로 적는다).

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
