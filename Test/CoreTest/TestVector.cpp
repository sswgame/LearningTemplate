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
