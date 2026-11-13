#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantCatalog.h"

using namespace sw;

namespace
{
    constexpr const utf8* kDishHead = "<Dish id=\"d\" name=\"D\" recipe=\"r\" ";
} // namespace

// 수 목록 칸도 속성 하나에 낱말을 잇는다.
SW_TEST_CASE( XMLFieldsNumberListTest, ReadsEveryToken )
{
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( string( kDishHead ) + "quality=\"3, 1;2\"/>" ) );
    DishDef dish;
    SW_REQUIRE_TRUE( XMLFields::readProperties( document.getRoot(), dish, "test" ) );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( dish._listQualityLevel.size() ) );
    SW_EXPECT_EQUAL( 3, dish._listQualityLevel[0] );
    SW_EXPECT_EQUAL( 2, dish._listQualityLevel[2] );
}

// 범위는 원소마다 본다 — 목록 안의 값 하나가 벗어나도 오류다.
SW_TEST_CASE( XMLFieldsNumberListTest, RejectsAnElementOutOfRange )
{
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( string( kDishHead ) + "quality=\"1, 0\"/>" ) );
    DishDef dish;
    SW_EXPECT_FALSE( XMLFields::readProperties( document.getRoot(), dish, "test" ) );
}

// 읽지 못한 낱말은 건너뛰지 않고 오류다.
SW_TEST_CASE( XMLFieldsNumberListTest, RejectsAnUnreadableToken )
{
    SW_EXPECT_LOG( "XMLFields.ValueUnreadable" );
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( string( kDishHead ) + "quality=\"1, x\"/>" ) );
    DishDef dish;
    SW_EXPECT_FALSE( XMLFields::readProperties( document.getRoot(), dish, "test" ) );
}
