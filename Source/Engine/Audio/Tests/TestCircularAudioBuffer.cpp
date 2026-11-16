// CircularAudioBufferTest — 웹 오디오 백엔드의 믹서 스레드 → AudioWorklet 링 버퍼(SPSC)를 비어 있음 · 가득 참 · 감김 · 두 스레드로 본다. 장치 없음.
#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "Engine/Audio/CircularAudioBuffer.h"

namespace
{
    struct CircularAudioBufferTestInternal
    {
        /** @brief (생산자 스레드) 0, 1, 2 … 를 @p totalSample 개까지 @p chunk 개씩 넣는다. 가득 차면 빈 자리가 날 때까지 다시 넣는다. */
        static void produceSequence( sw::CircularAudioBuffer* pBuffer, uint32 totalSample, uint32 chunk )
        {
            sw::vector<float32> listChunk( chunk );
            uint32              sent = 0;
            while ( sent < totalSample )
            {
                const uint32 count = totalSample - sent < chunk ? totalSample - sent : chunk;
                for ( uint32 offset = 0; offset < count; ++offset )
                {
                    listChunk[offset] = static_cast<float32>( sent + offset );
                }
                uint32 pushed = 0;
                while ( pushed < count )
                {
                    pushed += pBuffer->push( sw::vector_reference<const float32>( listChunk.data() + pushed, count - pushed ) );
                }
                sent += count;
            }
        }
    };
} // namespace

/** @brief [CircularAudioBufferTest] 용량은 2의 거듭제곱으로 오르고, 빈 버퍼는 아무것도 꺼내지 않는다. */
SW_TEST_CASE( CircularAudioBufferTest, EmptyPopsNothing )
{
    sw::CircularAudioBuffer buffer( 6 );
    SW_EXPECT_EQUAL( 8u, buffer.getCapacity() );
    SW_EXPECT_EQUAL( 0u, buffer.getNum() );
    SW_EXPECT_EQUAL( 8u, buffer.getRemainder() );

    float32 arrOut[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
    SW_EXPECT_EQUAL( 0u, buffer.pop( arrOut ) );
    SW_EXPECT_EQUAL( -1.0f, arrOut[0] ); // 꺼낼 것이 없으면 받는 쪽을 건드리지 않는다
}

/** @brief [CircularAudioBufferTest] 가득 차면 빈 자리만큼만 넣고, 꺼낸 만큼 다시 들어간다. */
SW_TEST_CASE( CircularAudioBufferTest, FullPushesOnlyFreeRoom )
{
    sw::CircularAudioBuffer buffer( 4 );
    const float32           arrIn[6] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
    SW_EXPECT_EQUAL( 4u, buffer.push( arrIn ) );
    SW_EXPECT_EQUAL( 0u, buffer.getRemainder() );
    SW_EXPECT_EQUAL( 0u, buffer.push( arrIn ) );

    float32 arrOut[1] = {};
    SW_EXPECT_EQUAL( 1u, buffer.pop( arrOut ) );
    SW_EXPECT_EQUAL( 1.0f, arrOut[0] );
    SW_EXPECT_EQUAL( 1u, buffer.push( sw::vector_reference<const float32>( arrIn + 4, 2 ) ) ); // 한 칸만 비었다
    SW_EXPECT_EQUAL( 4u, buffer.getNum() );
}

/** @brief [CircularAudioBufferTest] 끝을 넘어 감겨도 넣은 순서대로 나온다(묶음이 배열 끝에서 갈린다). */
SW_TEST_CASE( CircularAudioBufferTest, WrapKeepsOrder )
{
    sw::CircularAudioBuffer buffer( 8 );
    float32                 nextIn  = 0.0f;
    float32                 nextOut = 0.0f;
    for ( int32 round = 0; round < 20; ++round )
    {
        float32 arrIn[5] = {};
        for ( float32& sample : arrIn )
        {
            sample = nextIn;
            nextIn += 1.0f;
        }
        SW_REQUIRE_EQUAL( 5u, buffer.push( arrIn ) );
        float32 arrOut[5] = {};
        SW_REQUIRE_EQUAL( 5u, buffer.pop( arrOut ) );
        for ( const float32 sample : arrOut )
        {
            SW_REQUIRE_EQUAL( nextOut, sample );
            nextOut += 1.0f;
        }
    }
    SW_EXPECT_EQUAL( 0u, buffer.getNum() );
}

/** @brief [CircularAudioBufferTest] 생산자 · 소비자 스레드가 동시에 돌아도 샘플이 빠지거나 겹치지 않는다(믹서 스레드 → 워클릿 모양). */
SW_TEST_CASE( CircularAudioBufferTest, ProducerConsumerThreadsKeepSequence )
{
    constexpr uint32        kTotalSample = 1u << 18;
    constexpr uint32        kChunk       = 96; // 용량(256)의 약수가 아니라 묶음이 끝에서 갈린다
    sw::CircularAudioBuffer buffer( 256 );
    sw::thread              producer( sw::ThreadTestAccess::getKey(), &CircularAudioBufferTestInternal::produceSequence, &buffer, kTotalSample, kChunk );

    sw::vector<float32> listOut( 64 );
    uint32              received = 0;
    uint32              mismatch = 0;
    while ( received < kTotalSample )
    {
        const uint32 popped = buffer.pop( listOut );
        for ( uint32 offset = 0; offset < popped; ++offset )
        {
            if ( listOut[offset] != static_cast<float32>( received + offset ) )
                ++mismatch;
        }
        received += popped;
    }
    producer.join();
    SW_EXPECT_EQUAL( 0u, mismatch );
    SW_EXPECT_EQUAL( 0u, buffer.getNum() );
}
