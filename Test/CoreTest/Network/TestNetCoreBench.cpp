/**
 * @file TestNetCoreBench.cpp
 * @brief 넷 공통 계층 마이크로벤치 — 시퀀스 버퍼 칸 다시 쓰기(신뢰 메시지 페이로드), 비트 스트림 섞인 폭 쓰기 · 읽기.
 * @details 숫자를 **찍기만** 한다. Release 로 읽는다. 구조 최적화 B ①(칸 용량 유지) · ④(64 비트 워드)의 전/후를 잰다(표는 `docs/plans/StructuralOptimization.md`).
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/SequenceBuffer.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( NetCoreBenchTest, "net core micro-benchmarks - sequence buffer slot reuse, bit stream mixed widths" );
SW_TEST_TIME_BUDGET( NetCoreBenchTest, 2000, "측정 전 추정 — 첫 Release 실행 뒤 실제 최대의 네 배로 고친다" );

SW_LOG_CALLER( "NetCoreBench" );

namespace
{
    /** @brief 신뢰 메시지 칸 흉내(`NetConnection::OutgoingReliable` 과 같은 모양 — 바이트 버퍼 하나). */
    struct NetCoreBenchSlot
    {
        sw::vector<uint8> _buffer{};
        float64           _lastSentTime{ -1.0 };
    };

    struct NetCoreBenchInternal
    {
        static constexpr uint64 kInsertCount  = 200000;
        static constexpr uint32 kPayloadBytes = 200;
        static constexpr uint32 kFieldCount   = 4 * 100000;
        static constexpr uint32 kRoundCount   = 5;
    };
} // namespace

/**
 * @brief [NetCoreBenchTest] 시퀀스 버퍼에 칸을 넣고 200 바이트 페이로드를 담는 한 번 — 칸을 다시 쓸 때 버퍼를 새로 잡는지(구조 최적화 B ①)
 */
SW_TEST_CASE( NetCoreBenchTest, SequenceBufferSlotReuse )
{
    sw::SequenceBuffer<NetCoreBenchSlot> buffer;
    uint8                                arrPayload[NetCoreBenchInternal::kPayloadBytes]{};
    uint64                               totalBytes = 0;
    const int64                          deciNanos  = test::measureBestDeciNanosPerOp( NetCoreBenchInternal::kInsertCount, NetCoreBenchInternal::kRoundCount, [&buffer, &arrPayload, &totalBytes]()
                              {
        for ( uint64 index = 0; index < NetCoreBenchInternal::kInsertCount; ++index )
        {
            NetCoreBenchSlot* pSlot = buffer.insert( static_cast<uint16>( index ) );
            if ( pSlot == nullptr )
                continue;
            pSlot->_buffer.assign( arrPayload, arrPayload + NetCoreBenchInternal::kPayloadBytes );
            totalBytes += pSlot->_buffer.size();
        }
    } );
    test::logBenchDeciNanos( "sequence buffer insert + 200 B payload", deciNanos );
    SW_EXPECT_TRUE( totalBytes > 0 );
}

/**
 * @brief [NetCoreBenchTest] 1 · 7 · 13 · 32 비트 필드를 섞어 쓰고 읽는 한 필드(구조 최적화 B ④)
 */
SW_TEST_CASE( NetCoreBenchTest, BitStreamMixedFields )
{
    static constexpr int32 kArrWidth[] = { 1, 7, 13, 32 };
    sw::BitWriter          writer;
    writer.reserve( static_cast<int32>( NetCoreBenchInternal::kFieldCount * 7 ) );
    const int64 writeDeciNanos = test::measureBestDeciNanosPerOp( NetCoreBenchInternal::kFieldCount, NetCoreBenchInternal::kRoundCount, [&writer]()
    {
        writer.clear();
        for ( uint32 index = 0; index < NetCoreBenchInternal::kFieldCount; ++index )
        {
            writer.writeBits( ( index ^ ( index >> 3 ) ) + 0x55u, kArrWidth[index & 3] );
        }
    } );

    const sw::vector<uint8>& bytes         = writer.getBytes();
    uint32                   checksum      = 0;
    const int64              readDeciNanos = test::measureBestDeciNanosPerOp( NetCoreBenchInternal::kFieldCount, NetCoreBenchInternal::kRoundCount, [&bytes, &checksum]()
                 {
        sw::BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        for ( uint32 index = 0; index < NetCoreBenchInternal::kFieldCount; ++index )
        {
            checksum += reader.readBits( kArrWidth[index & 3] );
        }
    } );
    test::logBenchDeciNanos( "bit stream write, mixed 1/7/13/32 bit fields", writeDeciNanos );
    test::logBenchDeciNanos( "bit stream read, mixed 1/7/13/32 bit fields", readDeciNanos );
    SW_EXPECT_TRUE( checksum != 0 );
}
