#include "pch.h"

#include "Core/Container/vector.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct TrackedValue
    {
        static int32 s_liveCount;

        int32 _value;

        TrackedValue( int32 value = 0 )
            : _value{ value }
        {
            ++s_liveCount;
        }

        TrackedValue( const TrackedValue& other )
            : _value{ other._value }
        {
            ++s_liveCount;
        }

        TrackedValue( TrackedValue&& other ) noexcept
            : _value{ other._value }
        {
            ++s_liveCount;
        }

        ~TrackedValue()
        {
            --s_liveCount;
        }

        TrackedValue& operator=( const TrackedValue& other )
        {
            _value = other._value;
            return *this;
        }

        TrackedValue& operator=( TrackedValue&& other ) noexcept
        {
            _value = other._value;
            return *this;
        }
    };

    int32 TrackedValue::s_liveCount{ 0 };

    /** @brief 패딩이 끼는 POD — 바이트 복사 빠른 경로가 값을 그대로 옮기는지 보기 위한 것. */
    struct PaddedPod
    {
        uint8  _tag;
        uint64 _wide;
        uint8  _tail;

        bool operator==( const PaddedPod& other ) const
        {
            return _tag == other._tag && _wide == other._wide && _tail == other._tail;
        }
    };
} // namespace
/**
 * @brief [VectorTest] small_vector가 인라인 저장소와 힙 저장소 사이를 안전하게 이동한다.
 */
SW_TEST_CASE( VectorTest, SmallVectorStorageTransition )
{
    sw::small_vector<int32, 2> listValues{};
    const int32* const         pInlineData = listValues.data();

    listValues.push_back( 10 );
    listValues.push_back( 20 );
    SW_EXPECT_TRUE( listValues.data() == pInlineData );

    listValues.push_back( 30 );
    SW_EXPECT_TRUE( listValues.data() != pInlineData );
    SW_EXPECT_EQUAL( 3u, listValues.size() );
    SW_EXPECT_EQUAL( 10, listValues[0] );
    SW_EXPECT_EQUAL( 30, listValues[2] );

    listValues.pop_back();
    listValues.shrink_to_fit();
    SW_EXPECT_TRUE( listValues.data() == pInlineData );
    SW_EXPECT_EQUAL( 2u, listValues.size() );
}

/**
 * @brief [VectorTest] 복사·이동·삽입·삭제가 비트리비얼 타입의 수명을 보존한다.
 */
SW_TEST_CASE( VectorTest, ValueLifetimeAndMutation )
{
    {
        sw::vector<TrackedValue> listValues{};
        listValues.emplace_back( 10 );
        listValues.emplace_back( 30 );
        listValues.insert( listValues.begin() + 1, TrackedValue{ 20 } );
        listValues.erase( listValues.begin() );

        sw::vector<TrackedValue> copiedValues{ listValues };
        sw::vector<TrackedValue> movedValues{ std::move( copiedValues ) };
        SW_EXPECT_EQUAL( 2u, movedValues.size() );
        SW_EXPECT_EQUAL( 20, movedValues[0]._value );
        SW_EXPECT_EQUAL( 30, movedValues[1]._value );
    }

    SW_EXPECT_EQUAL( 0, TrackedValue::s_liveCount );
}

/**
 * @brief [VectorTest] small_vector 인라인 용량 내 기본 조작 검증
 */
SW_TEST_CASE( VectorTest, SmallVectorOperations )
{
    sw::small_vector<int32, 4> listSmall;
    SW_EXPECT_TRUE( listSmall.empty() );
    SW_EXPECT_EQUAL( 0u, listSmall.size() );
    SW_EXPECT_EQUAL( 4u, listSmall.capacity() );

    listSmall.push_back( 10 );
    listSmall.push_back( 20 );
    listSmall.push_back( 30 );
    listSmall.push_back( 40 );

    SW_EXPECT_EQUAL( 4u, listSmall.size() );
    SW_EXPECT_EQUAL( 10, listSmall.front() );
    SW_EXPECT_EQUAL( 40, listSmall.back() );
    SW_EXPECT_EQUAL( 20, listSmall[1] );
    SW_EXPECT_EQUAL( 30, listSmall.at( 2 ) );

    listSmall.pop_back();
    SW_EXPECT_EQUAL( 3u, listSmall.size() );
    SW_EXPECT_EQUAL( 30, listSmall.back() );

    listSmall.clear();
    SW_EXPECT_TRUE( listSmall.empty() );
    SW_EXPECT_EQUAL( 0u, listSmall.size() );
}

/**
 * @brief [VectorTest] vector 초기화 리스트, fill 생성자, reserve, 범위 조작 검증
 */
SW_TEST_CASE( VectorTest, VectorConstructorsAndRangeOperations )
{
    // 1) 초기화 리스트 생성자
    sw::vector<int32> listInit{ 1, 2, 3, 4, 5 };
    SW_EXPECT_EQUAL( 5u, listInit.size() );
    SW_EXPECT_EQUAL( 1, listInit[0] );
    SW_EXPECT_EQUAL( 5, listInit[4] );

    // 2) 채우기(fill) 생성자
    sw::vector<int32> listFill( 4, 100 );
    SW_EXPECT_EQUAL( 4u, listFill.size() );
    for ( const int32 val : listFill )
    {
        SW_EXPECT_EQUAL( 100, val );
    }

    // 3) reserve 및 capacity
    listInit.reserve( 32 );
    SW_EXPECT_TRUE( listInit.capacity() >= 32u );
    SW_EXPECT_EQUAL( 5u, listInit.size() );

    // 4) erase 및 insert
    listInit.erase( listInit.begin() + 2 ); // 3 제거 -> { 1, 2, 4, 5 }
    SW_EXPECT_EQUAL( 4u, listInit.size() );
    SW_EXPECT_EQUAL( 4, listInit[2] );

    listInit.insert( listInit.begin() + 2, 99 ); // { 1, 2, 99, 4, 5 }
    SW_EXPECT_EQUAL( 5u, listInit.size() );
    SW_EXPECT_EQUAL( 99, listInit[2] );
}
/**
 * @brief [VectorTest] 바이트 복사 빠른 경로가 값을 그대로 옮긴다.
 * @details `vector` 는 복사에도 소멸에도 사용자 코드가 없는 타입이면 원소 루프 대신 `Memory::copy`
 *          한 번으로 옮긴다. 그 경로가 크기·용량·값을 바꾸지 않는지 고정한다 — 빈 대상과
 *          **이미 원소가 든 대상**(clear 뒤 재사용) 양쪽을 본다. 후자가 매 프레임 렌더 패킷이 타는 길이다.
 */
SW_TEST_CASE( VectorTest, BitwiseCopyKeepsValues )
{
    static_assert( sw::is_bitwise_copyable_v<PaddedPod>, "PaddedPod 는 바이트 복사 대상이어야 한다" );
    static_assert( sw::is_bitwise_copyable_v<TrackedValue> == false, "TrackedValue 는 루프 경로여야 한다" );

    sw::vector<PaddedPod> listSource{};
    for ( uint32 index = 0; index < 64; ++index )
        listSource.push_back( PaddedPod{ static_cast<uint8>( index ), index * 1000003ull, static_cast<uint8>( 255 - index ) } );

    // 1) 복사 생성
    sw::vector<PaddedPod> listCopy{ listSource };
    SW_ASSERT_EQUAL( listSource.size(), listCopy.size() );
    for ( size_t index = 0; index < listSource.size(); ++index )
        SW_EXPECT_TRUE( listSource[index] == listCopy[index] );

    // 2) 이미 내용이 있는 대상에 복사 대입 — 길이가 줄어드는 쪽도 본다.
    sw::vector<PaddedPod> listTarget{};
    for ( uint32 index = 0; index < 200; ++index )
        listTarget.push_back( PaddedPod{ 7, 7, 7 } );
    listTarget = listSource;
    SW_ASSERT_EQUAL( listSource.size(), listTarget.size() );
    for ( size_t index = 0; index < listSource.size(); ++index )
        SW_EXPECT_TRUE( listSource[index] == listTarget[index] );

    // 3) 빈 원본을 대입하면 비어야 한다.
    const sw::vector<PaddedPod> listEmpty{};
    listTarget = listEmpty;
    SW_EXPECT_TRUE( listTarget.empty() );
}

/**
 * @brief [VectorTest] 루프 경로는 생성자·소멸자 짝을 그대로 지킨다.
 * @details 빠른 경로를 넣으면서 **분기를 잘못 태우면** 여기서 살아 있는 개수가 어긋난다.
 *          성장(재할당)과 복사 대입 둘 다 통과시킨다.
 */
SW_TEST_CASE( VectorTest, NonTrivialCopyKeepsLifetimeBalance )
{
    TrackedValue::s_liveCount = 0;
    {
        sw::vector<TrackedValue> listSource{};
        for ( int32 index = 0; index < 100; ++index ) // 여러 번 재할당된다
            listSource.push_back( TrackedValue{ index } );
        SW_EXPECT_EQUAL( 100, TrackedValue::s_liveCount );

        sw::vector<TrackedValue> listTarget{};
        listTarget.push_back( TrackedValue{ -1 } );
        listTarget = listSource;
        SW_ASSERT_EQUAL( size_t( 100 ), listTarget.size() );
        SW_EXPECT_EQUAL( 200, TrackedValue::s_liveCount );
        for ( int32 index = 0; index < 100; ++index )
            SW_EXPECT_EQUAL( index, listTarget[static_cast<size_t>( index )]._value );
    }
    SW_EXPECT_EQUAL( 0, TrackedValue::s_liveCount );
}

/**
 * @brief [VectorTest] 가진 것보다 많이 끼워 넣어도 범위 밖을 읽지 않는다
 * @details `insert( pos, count, value )` 의 첫 루프가 `itemIndex - count >= offset` 으로 "옮길
 *          원소인가" 를 갈랐다. `count > _size` 면 그 뺄셈이 **size_t 로 뒤집혀** 조건이 언제나
 *          참이 되고, 존재하지도 않는 `_pData[2^64-k]` 에서 move 해 온다. 앞쪽에 두 개만 끼워
 *          넣어도(`{1} 에 insert(begin, 2, ...)`) 바로 걸린다.
 */
SW_TEST_CASE( VectorTest, InsertMoreThanSizeDoesNotReadOutOfBounds )
{
    {
        sw::vector<int32> list{ 10 };
        list.insert( list.begin(), 2, 7 );
        SW_EXPECT_EQUAL( size_t( 3 ), list.size() );
        SW_EXPECT_EQUAL( 7, list[0] );
        SW_EXPECT_EQUAL( 7, list[1] );
        SW_EXPECT_EQUAL( 10, list[2] );
    }

    {
        sw::vector<int32> list{ 1, 2 };
        list.insert( list.begin() + 1, 5, 9 );
        SW_EXPECT_EQUAL( size_t( 7 ), list.size() );
        SW_EXPECT_EQUAL( 1, list[0] );
        for ( size_t index = 1; index <= 5; ++index )
            SW_EXPECT_EQUAL( 9, list[index] );
        SW_EXPECT_EQUAL( 2, list[6] );
    }

    // 정상 경우(count <= size)도 그대로여야 한다.
    {
        sw::vector<int32> list{ 1, 2, 3, 4, 5 };
        list.insert( list.begin() + 1, 2, 0 );
        const int32 arrExpected[] = { 1, 0, 0, 2, 3, 4, 5 };
        SW_ASSERT_EQUAL( size_t( 7 ), list.size() );
        for ( size_t index = 0; index < list.size(); ++index )
            SW_EXPECT_EQUAL( arrExpected[index], list[index] );
    }
}

/**
 * @brief [VectorTest] 0개를 끼워 넣는 것은 아무 일도 하지 않는다
 * @details count 가 0 이면 두 번째 루프의 종료 조건이 `itemIndex >= offset + 0` 이 된다. offset 이 0
 *          이면 **언제나 참**이라 `itemIndex` 가 0 에서 한 번 더 줄어 size_t 로 뒤집히고, 범위 밖에
 *          계속 쓰면서 끝나지 않는다. 그 전에 `_pData[i] = move(_pData[i])` 라는 자기 자신으로의
 *          이동 대입도 돈다.
 */
SW_TEST_CASE( VectorTest, InsertZeroCountIsANoOp )
{
    sw::vector<int32> list{ 1, 2, 3 };
    const auto        iter = list.insert( list.begin(), 0, 99 );

    SW_EXPECT_EQUAL( size_t( 3 ), list.size() );
    SW_EXPECT_EQUAL( 1, list[0] );
    SW_EXPECT_EQUAL( 2, list[1] );
    SW_EXPECT_EQUAL( 3, list[2] );
    SW_EXPECT_TRUE( iter == list.begin() );

    // 가운데·끝에서도 마찬가지다.
    list.insert( list.begin() + 1, 0, 99 );
    list.insert( list.end(), 0, 99 );
    SW_EXPECT_EQUAL( size_t( 3 ), list.size() );
    SW_EXPECT_EQUAL( 2, list[1] );
}

/**
 * @brief [VectorTest] 자기 안의 원소를 끼워 넣어도 된다
 * @details `v.insert( v.begin(), 3, v[0] )` 는 적법한 호출이다. 그런데 그 자리를 덮어쓰기도 하고,
 *          그 전에 재할당이 버퍼를 통째로 옮기기도 한다 — 참조로 들고 있으면 둘 중 하나에서
 *          **이미 사라진 값**을 복사하게 된다. 손대기 전에 값으로 떠 둔다.
 */
SW_TEST_CASE( VectorTest, InsertAcceptsAnElementOfItself )
{
    // 재할당이 반드시 일어나도록 용량을 딱 맞춰 둔다.
    sw::vector<sw::string> list;
    list.reserve( 2 );
    list.push_back( sw::string( "alpha" ) );
    list.push_back( sw::string( "beta" ) );
    SW_ASSERT_EQUAL( size_t( 2 ), list.capacity() );

    list.insert( list.begin(), 3, list[0] );

    SW_ASSERT_EQUAL( size_t( 5 ), list.size() );
    SW_EXPECT_STREQ( "alpha", list[0].c_str() );
    SW_EXPECT_STREQ( "alpha", list[1].c_str() );
    SW_EXPECT_STREQ( "alpha", list[2].c_str() );
    SW_EXPECT_STREQ( "alpha", list[3].c_str() );
    SW_EXPECT_STREQ( "beta", list[4].c_str() );
}

/**
 * @brief [VectorTest] 이동 삽입의 원본이 이 벡터 안의 원소여도 올바른 값이 들어가는지 검증
 * @details `insert( pos, count, value )` 는 값을 먼저 떠 두는데(그 주석에 `v.insert( v.begin(),
 *          3, v[0] )` 이 적법하다고 적혀 있다) **이동 오버로드만 그러지 않았다.**
 *          `push_back` 둘과 `emplace_back` 도 전부 떠 두므로, 이 한 판만 빠져 있었다.
 *
 *          재할당이 없어도 틀린다 — 밀기 루프가 `value` 가 가리키는 칸을 **먼저 덮기** 때문이다.
 *          재할당까지 겹치면 옛 버퍼가 해제된 뒤라 죽은 자리를 읽는다(ASAN).
 */
SW_TEST_CASE( VectorTest, InsertMoveAcceptsAnElementOfItself )
{
    BLOCK( "재할당 없이 — 밀기 루프가 원본 칸을 덮는다" )
    {
        sw::vector<sw::string> list;
        list.reserve( 8 );
        list.push_back( sw::string( "alpha" ) );
        list.push_back( sw::string( "bravo" ) );
        list.push_back( sw::string( "charlie" ) );
        list.push_back( sw::string( "delta" ) );
        SW_ASSERT_TRUE( list.capacity() >= 5 );

        list.insert( list.begin(), std::move( list[2] ) );

        SW_ASSERT_EQUAL( size_t( 5 ), list.size() );
        // 고치기 전에는 여기가 "bravo" 였다 — 밀기 루프가 index 2 를 이미 덮은 뒤였다.
        SW_EXPECT_STREQ( "charlie", list[0].c_str() );
        SW_EXPECT_STREQ( "alpha", list[1].c_str() );
        SW_EXPECT_STREQ( "bravo", list[2].c_str() );
        SW_EXPECT_STREQ( "delta", list[4].c_str() );
    }

    BLOCK( "재할당과 함께 — 옛 버퍼가 해제된 뒤 읽는다" )
    {
        sw::vector<sw::string> list;
        list.reserve( 4 );
        list.push_back( sw::string( 64, 'a' ) );
        list.push_back( sw::string( 64, 'b' ) );
        list.push_back( sw::string( 64, 'c' ) );
        list.push_back( sw::string( 64, 'd' ) );
        SW_ASSERT_EQUAL( size_t( 4 ), list.capacity() );

        list.insert( list.begin(), std::move( list[3] ) );

        SW_ASSERT_EQUAL( size_t( 5 ), list.size() );
        SW_EXPECT_STREQ( sw::string( 64, 'd' ).c_str(), list[0].c_str() );
        SW_EXPECT_STREQ( sw::string( 64, 'a' ).c_str(), list[1].c_str() );
    }
}

/**
 * @brief [VectorTest] 빈 벡터에서 `pop_back` 이 범위 밖을 건드리지 않는지 검증
 * @details `SW_ASSERT( _size > 0 )` 뿐이었다 — **Release 에서는 통째로 사라진다.** 그러면
 *          `_pData[_size - 1].~T()` 의 첨자가 뒤집혀 `_pData[SIZE_MAX]` 의 소멸자를 부른다.
 *          바로 위 `erase` 가 같은 이유로 진짜 가드를 들고 있는데 이쪽은 없었다.
 */
SW_TEST_CASE( VectorTest, PopBackOnAnEmptyVectorIsSafe )
{
#if defined( SW_DEBUG )
    // Debug 에서는 `SW_ASSERT( _size > 0 )` 가 먼저 울려 프로세스를 세운다 — **그것이 의도다.**
    // 이 가드는 단언이 통째로 사라지는 빌드를 위한 것이라 거기서만 잴 수 있다.
    SW_TEST_SKIP( "SW_ASSERT stops the process in Debug; the guard only matters where the assert is gone." );
#else
    sw::vector<sw::string> list;
    list.pop_back();
    SW_EXPECT_EQUAL( size_t( 0 ), list.size() );

    list.push_back( sw::string( "only" ) );
    list.pop_back();
    list.pop_back();
    SW_EXPECT_EQUAL( size_t( 0 ), list.size() );
#endif
}

/**
 * @brief [VectorTest] 뒤집힌 이터레이터 쌍과 거대한 개수가 첨자를 접지 않는지 검증
 * @details 두 가드가 덧셈 형태였다 — `erase` 는 `offset + count > _size`, `insert` 는
 *          `_size + count > _capacity`. `size_t` 안에서 그 합이 넘치면 **작은 수로 접혀**
 *          가드를 그냥 지나간다. `last < first` 인 이터레이터 쌍이면 `last - first` 가 음수라
 *          `count` 가 거대해지고(뒤집힌 뺄셈으로 만든 개수도 같다), 그 뒤 `_size - count` 와
 *          `fromIndex + count` 가 전부 범위 밖을 가리킨다.
 *
 *          같은 모양을 `fixed_string::erase` 에서도 고쳤다 — 이 저장소가 되풀이해 만난 형태다.
 */
SW_TEST_CASE( VectorTest, ReversedRangeAndHugeCountDoNotWrap )
{
    BLOCK( "erase — last 가 first 보다 앞이면 아무것도 하지 않는다" )
    {
        sw::vector<sw::string> list;
        for ( int32 index = 0; index < 5; ++index )
            list.push_back( sw::string( 1, static_cast<utf8>( 'a' + index ) ) );

        list.erase( list.begin() + 3, list.begin() + 1 );

        SW_EXPECT_EQUAL( size_t( 5 ), list.size() );
        SW_EXPECT_STREQ( "a", list[0].c_str() );
        SW_EXPECT_STREQ( "e", list[4].c_str() );
    }

    BLOCK( "erase — 정상 범위는 그대로 동작한다" )
    {
        sw::vector<sw::string> list;
        for ( int32 index = 0; index < 5; ++index )
            list.push_back( sw::string( 1, static_cast<utf8>( 'a' + index ) ) );

        list.erase( list.begin() + 1, list.begin() + 3 );

        SW_EXPECT_EQUAL( size_t( 3 ), list.size() );
        SW_EXPECT_STREQ( "a", list[0].c_str() );
        SW_EXPECT_STREQ( "d", list[1].c_str() );
        SW_EXPECT_STREQ( "e", list[2].c_str() );
    }

    BLOCK( "insert — 담을 수 없는 개수는 아무것도 하지 않는다" )
    {
        sw::vector<int32> list;
        list.push_back( 1 );
        list.push_back( 2 );

        list.insert( list.begin(), ~size_t{ 0 }, 7 );

        SW_EXPECT_EQUAL( size_t( 2 ), list.size() );
        SW_EXPECT_EQUAL( 1, list[0] );
        SW_EXPECT_EQUAL( 2, list[1] );
    }
}
