#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

// GameRandomTest — 게임 규칙 난수의 도우미: 정수 · 실수 가중치 고르기(0 · 음수 가중치는 고르지 않음, 합 0 은 -1, 비율, 난수를 한 번만 씀),
// Fisher-Yates 섞기(결정성 · 순열 · 난수 n - 1 번). 좌표 해시 · 값 노이즈는 Core(`Core/Math/Tests/TestValueNoise.cpp`). nogpu.

namespace
{
    struct WeightedItem
    {
        int32 _weight{ 0 };
    };

    int32 getWeight( const WeightedItem& item )
    {
        return item._weight;
    }
} // namespace

/**
 * @brief [GameRandomTest] 정수 가중치: 0 · 음수는 고르지 않고, 합이 0 이하이거나 비었으면 -1 이며, 고른 비율이 가중치를 따른다
 */
SW_TEST_CASE( GameRandomTest, IntWeightsSkipNonPositiveAndFollowRatio )
{
    sw::GameRandom                 random( 2024u );
    const sw::vector<WeightedItem> listItem   = { { 1 }, { 0 }, { -5 }, { 3 } };
    int32                          arrHit[4]  = {};
    constexpr int32                kDrawCount = 4000;
    for ( int32 draw = 0; draw < kDrawCount; ++draw )
    {
        const int32 index = random.pickWeightedIndexInt( listItem, getWeight );
        SW_REQUIRE_TRUE( index == 0 || index == 3 );
        ++arrHit[index];
    }
    SW_EXPECT_EQUAL( 0, arrHit[1] );
    SW_EXPECT_EQUAL( 0, arrHit[2] );
    SW_EXPECT_NEAR( 0.25, static_cast<float64>( arrHit[0] ) / kDrawCount, 0.03 );

    const sw::vector<WeightedItem> listZero = { { 0 }, { -1 } };
    SW_EXPECT_EQUAL( -1, random.pickWeightedIndexInt( listZero, getWeight ) );
    SW_EXPECT_EQUAL( -1, random.pickWeightedIndexInt( sw::vector<WeightedItem>{}, getWeight ) );
    const sw::vector<WeightedItem> listOne = { { 0 }, { 7 }, { 0 } };
    SW_EXPECT_EQUAL( 1, random.pickWeightedIndexInt( listOne, getWeight ) );
}

/**
 * @brief [GameRandomTest] 가중치 고르기는 난수를 정확히 한 번 쓴다(정수는 `nextInt( 0, 합 - 1 )`, 실수는 `nextFloat`) — 고르기가 끼어도 뒤 수열이 같다. 합 0 이면 쓰지 않는다
 */
SW_TEST_CASE( GameRandomTest, WeightedPickConsumesExactlyOneDraw )
{
    const sw::vector<WeightedItem> listItem = { { 2 }, { 5 }, { 1 } };

    sw::GameRandom picked( 99u );
    sw::GameRandom manual( 99u );
    (void)picked.pickWeightedIndexInt( listItem, getWeight );
    (void)manual.nextInt( 0, 7 );
    SW_EXPECT_EQUAL( manual.getState(), picked.getState() );

    (void)picked.pickWeightedIndex( listItem, getWeight );
    (void)manual.nextFloat();
    SW_EXPECT_EQUAL( manual.getState(), picked.getState() );

    const uint32                   stateBefore = picked.getState();
    const sw::vector<WeightedItem> listZero    = { { 0 } };
    SW_EXPECT_EQUAL( -1, picked.pickWeightedIndex( listZero, getWeight ) );
    SW_EXPECT_EQUAL( -1, picked.pickWeightedIndexInt( listZero, getWeight ) );
    SW_EXPECT_EQUAL( stateBefore, picked.getState() );
}

/**
 * @brief [GameRandomTest] 실수 가중치도 0 · 음수는 고르지 않고 양수 하나뿐이면 늘 그것이다
 */
SW_TEST_CASE( GameRandomTest, FloatWeightsNeverPickNonPositive )
{
    sw::GameRandom                 random( 5u );
    const sw::vector<WeightedItem> listItem  = { { 0 }, { 4 }, { -2 }, { 1 } };
    int32                          arrHit[4] = {};
    for ( int32 draw = 0; draw < 2000; ++draw )
    {
        const int32 index = random.pickWeightedIndex( listItem, getWeight );
        SW_REQUIRE_TRUE( 0 <= index && index < 4 );
        ++arrHit[index];
    }
    SW_EXPECT_EQUAL( 0, arrHit[0] );
    SW_EXPECT_EQUAL( 0, arrHit[2] );
    SW_EXPECT_NEAR( 0.8, static_cast<float64>( arrHit[1] ) / 2000.0, 0.04 );

    const sw::vector<WeightedItem> listLast = { { 0 }, { 0 }, { 3 } };
    for ( int32 draw = 0; draw < 50; ++draw )
    {
        SW_EXPECT_EQUAL( 2, random.pickWeightedIndex( listLast, getWeight ) );
    }
}

/**
 * @brief [GameRandomTest] 섞기는 같은 씨앗이면 같은 순서, 원소를 잃거나 겹치지 않으며, 난수를 n - 1 번 쓴다. 0 · 1 개는 난수를 쓰지 않는다
 */
SW_TEST_CASE( GameRandomTest, ShuffleIsDeterministicPermutation )
{
    sw::vector<int32> listFirst;
    for ( int32 value = 0; value < 20; ++value )
    {
        listFirst.push_back( value );
    }
    sw::vector<int32> listSecond = listFirst;

    sw::GameRandom randomA( 31337u );
    sw::GameRandom randomB( 31337u );
    randomA.shuffle( listFirst );
    randomB.shuffle( listSecond );
    SW_EXPECT_TRUE( listFirst == listSecond );

    bool arrSeen[20] = {};
    for ( const int32 value : listFirst )
    {
        SW_REQUIRE_TRUE( 0 <= value && value < 20 );
        SW_EXPECT_FALSE( arrSeen[value] );
        arrSeen[value] = true;
    }

    sw::GameRandom manual( 31337u );
    for ( int32 index = 19; index > 0; --index )
    {
        (void)manual.nextInt( 0, index );
    }
    SW_EXPECT_EQUAL( manual.getState(), randomA.getState() );

    const uint32      stateBefore = randomA.getState();
    sw::vector<int32> listEmpty;
    sw::vector<int32> listSingle = { 7 };
    randomA.shuffle( listEmpty );
    randomA.shuffle( listSingle );
    SW_EXPECT_EQUAL( stateBefore, randomA.getState() );
    SW_EXPECT_EQUAL( 7, listSingle[0] );
}
