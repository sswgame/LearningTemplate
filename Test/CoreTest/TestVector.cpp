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
} // namespace

/**
 * @brief [Core_Vector] small_vector가 인라인 저장소와 힙 저장소 사이를 안전하게 이동한다.
 */
SW_TEST_CASE( Core_Vector, SmallVectorStorageTransition )
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
 * @brief [Core_Vector] 복사·이동·삽입·삭제가 비트리비얼 타입의 수명을 보존한다.
 */
SW_TEST_CASE( Core_Vector, ValueLifetimeAndMutation )
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
 * @brief [Core_Vector] small_vector 인라인 용량 내 기본 조작 검증
 */
SW_TEST_CASE( Core_Vector, SmallVectorOperations )
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
 * @brief [Core_Vector] vector 초기화 리스트, fill 생성자, reserve, 범위 조작 검증
 */
SW_TEST_CASE( Core_Vector, VectorConstructorsAndRangeOperations )
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
