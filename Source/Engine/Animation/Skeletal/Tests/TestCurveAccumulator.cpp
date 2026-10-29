#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Skeletal/CurveAccumulator.h"

using namespace sw;

/**
 * @brief [CurveAccumulatorTest] 이름별 누적이 옛 선형 탐색 누적과 같은 이름 순서 · 같은 값을 낸다
 * @details 옛 모양(`VectorUtil::findIndex` 로 이름 목록을 훑어 더한다)을 이 테스트 안에 그대로 두고 같은 입력을 먹여 비교한다.
 */
SW_TEST_CASE( CurveAccumulatorTest, MatchesLinearFindAccumulation )
{
    const utf8* arrName[] = { "Smile", "Blink", "Jaw", "Smile", "Brow", "Blink", "Smile", "Jaw" };

    vector<hashed_string> listOldName;
    vector<float32>       listOldValue;
    CurveAccumulator      accumulator;
    float32               value = 0.25f;
    for ( const utf8* pName : arrName )
    {
        const hashed_string name( pName );
        value += 0.5f;
        accumulator.add( name, value );

        const std::optional<size_t> found = VectorUtil::findIndex( listOldName, name );
        if ( found.has_value() == false )
        {
            listOldName.push_back( name );
            listOldValue.push_back( value );
        }
        else
        {
            listOldValue[*found] += value;
        }
    }

    SW_REQUIRE_EQUAL( listOldName.size(), accumulator.getNames().size() );
    SW_REQUIRE_EQUAL( listOldValue.size(), accumulator.getValues().size() );
    for ( size_t index = 0; index < listOldName.size(); ++index )
    {
        SW_EXPECT_TRUE( listOldName[index] == accumulator.getNames()[index] );
        SW_EXPECT_EQUAL( listOldValue[index], accumulator.getValues()[index] );
        SW_EXPECT_EQUAL( listOldValue[index], accumulator.getValue( listOldName[index] ) );
    }
    SW_EXPECT_EQUAL( 0.0f, accumulator.getValue( hashed_string( "Missing" ) ) );
}

/**
 * @brief [CurveAccumulatorTest] clear 뒤에는 이름 · 값 · 인덱스가 모두 비어 다시 처음 보는 이름처럼 쌓인다
 */
SW_TEST_CASE( CurveAccumulatorTest, ClearForgetsNamesAndIndices )
{
    CurveAccumulator accumulator;
    accumulator.add( hashed_string( "A" ), 1.0f );
    accumulator.add( hashed_string( "B" ), 2.0f );
    accumulator.clear();
    SW_EXPECT_TRUE( accumulator.getNames().empty() );
    SW_EXPECT_TRUE( accumulator.getValues().empty() );

    accumulator.add( hashed_string( "B" ), 5.0f );
    SW_REQUIRE_EQUAL( size_t( 1 ), accumulator.getNames().size() );
    SW_EXPECT_EQUAL( 5.0f, accumulator.getValue( hashed_string( "B" ) ) );
    SW_EXPECT_EQUAL( 0.0f, accumulator.getValue( hashed_string( "A" ) ) );
}
