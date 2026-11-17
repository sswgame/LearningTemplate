// deque — 연속 링(감사 C-9): 감아 돌기 · 키우기 순서, std::deque 와 무작위 연산 대조, 원소 수명 균형, 반복자.
#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Testing/TestFramework.h"

#include <deque>

using namespace sw;

namespace
{
    /** @brief 살아 있는 개수를 셉니다(링이 옮기고 지울 때 생성 · 소멸이 맞는지 본다). */
    struct LiveCountValue
    {
        static int32 s_liveCount;

        int32 _value = 0;

        LiveCountValue( int32 value = 0 )
            : _value{ value }
        {
            ++s_liveCount;
        }
        LiveCountValue( const LiveCountValue& other )
            : _value{ other._value }
        {
            ++s_liveCount;
        }
        LiveCountValue( LiveCountValue&& other ) noexcept
            : _value{ other._value }
        {
            ++s_liveCount;
        }
        LiveCountValue& operator=( const LiveCountValue& other )     = default;
        LiveCountValue& operator=( LiveCountValue&& other ) noexcept = default;
        ~LiveCountValue() { --s_liveCount; }

        bool operator==( const LiveCountValue& other ) const { return _value == other._value; }
    };

    int32 LiveCountValue::s_liveCount = 0;

    /** @brief 테스트용 결정적 난수(선형 합동)입니다. */
    struct TestRandomInternal
    {
        uint32 _state = 12345u;
        uint32 next( uint32 bound )
        {
            _state = _state * 1664525u + 1013904223u;
            return ( _state >> 8 ) % bound;
        }
    };
} // namespace

/**
 * @brief [DequeTest] 앞뒤로 감아 돌며 넣고 빼다 커져도 순서가 그대로다
 */
SW_TEST_CASE( DequeTest, WrapsAndGrowsKeepingOrder )
{
    sw::deque<int32> queue;
    // 머리가 버퍼 끝 근처에 오게 한 뒤(넣고 빼기) 앞뒤로 넣어 감아 돌기 + 키우기가 겹치게 한다.
    for ( int32 index = 0; index < 6; ++index )
        queue.push_back( index );
    for ( int32 index = 0; index < 6; ++index )
        queue.pop_front();
    SW_EXPECT_TRUE( queue.empty() );

    for ( int32 index = 0; index < 20; ++index )
    {
        queue.push_back( 100 + index );
        queue.push_front( -index );
    }
    SW_REQUIRE_EQUAL( size_t( 40 ), queue.size() );
    for ( int32 index = 0; index < 20; ++index )
    {
        SW_EXPECT_EQUAL( -( 19 - index ), queue[static_cast<size_t>( index )] );
        SW_EXPECT_EQUAL( 100 + index, queue[static_cast<size_t>( 20 + index )] );
    }

    // FIFO 줄: 용량이 찬 뒤로는 키우지 않는다.
    sw::deque<int32> slidingQueue;
    slidingQueue.reserve( 64 );
    const size_t capacity = slidingQueue.capacity();
    for ( int32 index = 0; index < 10000; ++index )
    {
        slidingQueue.push_back( index );
        if ( slidingQueue.size() > 32 )
        {
            SW_EXPECT_EQUAL( index - 32, slidingQueue.front() );
            slidingQueue.pop_front();
        }
    }
    SW_EXPECT_EQUAL( capacity, slidingQueue.capacity() );
}

/**
 * @brief [DequeTest] 무작위 넣기 · 빼기 · 가운데 insert · erase 가 std::deque 와 같은 결과를 내고, 원소 수명이 맞는다
 */
SW_TEST_CASE( DequeTest, EraseAndInsertMatchStdDeque )
{
    LiveCountValue::s_liveCount = 0;
    {
        sw::deque<LiveCountValue> actual;
        std::deque<int32>         expected;
        TestRandomInternal        random;
        for ( int32 step = 0; step < 4000; ++step )
        {
            const int32 value = step;
            switch ( random.next( 8 ) )
            {
                case 0:
                case 1:
                    actual.push_back( LiveCountValue{ value } );
                    expected.push_back( value );
                    break;
                case 2:
                    actual.emplace_front( value );
                    expected.push_front( value );
                    break;
                case 3:
                    if ( expected.empty() == false )
                    {
                        actual.pop_front();
                        expected.pop_front();
                    }
                    break;
                case 4:
                    if ( expected.empty() == false )
                    {
                        actual.pop_back();
                        expected.pop_back();
                    }
                    break;
                case 5:
                {
                    const uint32 at = random.next( static_cast<uint32>( expected.size() ) + 1 );
                    actual.insert( actual.begin() + at, LiveCountValue{ value } );
                    expected.insert( expected.begin() + at, value );
                    break;
                }
                case 6:
                {
                    const uint32 at    = random.next( static_cast<uint32>( expected.size() ) + 1 );
                    const uint32 count = random.next( 4 );
                    actual.insert( actual.begin() + at, count, LiveCountValue{ value } );
                    expected.insert( expected.begin() + at, count, value );
                    break;
                }
                default:
                {
                    if ( expected.empty() )
                        break;
                    const uint32 at    = random.next( static_cast<uint32>( expected.size() ) );
                    const uint32 count = 1 + random.next( MathUtil::min( 4u, static_cast<uint32>( expected.size() ) - at ) );
                    const auto   iter  = actual.erase( actual.begin() + at, actual.begin() + at + count );
                    expected.erase( expected.begin() + at, expected.begin() + at + count );
                    SW_EXPECT_TRUE( iter == actual.begin() + at );
                    break;
                }
            }
            SW_REQUIRE_EQUAL( expected.size(), actual.size() );
        }
        for ( size_t index = 0; index < expected.size(); ++index )
            SW_EXPECT_EQUAL( expected[index], actual[index]._value );
        SW_EXPECT_EQUAL( static_cast<int32>( expected.size() ), LiveCountValue::s_liveCount );

        // 복사 · 이동 · 범위 삽입(자기 범위)도 같은 순서다.
        sw::deque<LiveCountValue> copied = actual;
        SW_EXPECT_TRUE( copied == actual );
        sw::deque<LiveCountValue> moved = std::move( copied );
        SW_EXPECT_TRUE( copied.empty() );
        moved.insert( moved.begin() + 1, actual.begin(), actual.begin() + 3 );
        std::deque<int32> expectedMoved = expected;
        expectedMoved.insert( expectedMoved.begin() + 1, expected.begin(), expected.begin() + 3 );
        SW_REQUIRE_EQUAL( expectedMoved.size(), moved.size() );
        for ( size_t index = 0; index < expectedMoved.size(); ++index )
            SW_EXPECT_EQUAL( expectedMoved[index], moved[index]._value );
    }
    SW_EXPECT_EQUAL( 0, LiveCountValue::s_liveCount );
}

/**
 * @brief [DequeTest] 역방향 · const 반복자, resize, shrink_to_fit, 자기 원소 push 가 맞다
 */
SW_TEST_CASE( DequeTest, IteratorsResizeAndSelfReference )
{
    sw::deque<string> queue{ "a", "b", "c" };
    string            joined;
    for ( auto iter = queue.rbegin(); iter != queue.rend(); ++iter )
        joined += *iter;
    SW_EXPECT_TRUE( joined == "cba" );

    const sw::deque<string>&          constQueue = queue;
    sw::deque<string>::const_iterator constIter  = queue.begin(); // iterator → const_iterator
    SW_EXPECT_TRUE( constIter == constQueue.begin() );
    SW_EXPECT_EQUAL( static_cast<std::ptrdiff_t>( 3 ), constQueue.end() - constQueue.begin() );

    // 꽉 찬 상태에서 자기 원소를 넣으면 키우기 전에 떠 둬야 한다.
    sw::deque<string> full;
    for ( int32 index = 0; index < 8; ++index )
        full.push_back( "x" );
    SW_REQUIRE_EQUAL( full.size(), full.capacity() );
    full.front() = "first";
    full.push_back( full.front() );
    SW_EXPECT_TRUE( full.back() == "first" );

    queue.resize( 5 );
    SW_EXPECT_EQUAL( size_t( 5 ), queue.size() );
    SW_EXPECT_TRUE( queue[4].empty() );
    queue.resize( 2 );
    queue.shrink_to_fit();
    SW_EXPECT_EQUAL( size_t( 2 ), queue.size() );
    SW_EXPECT_TRUE( queue[0] == "a" && queue[1] == "b" );
}
