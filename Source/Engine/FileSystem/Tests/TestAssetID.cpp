#include "pch.h"

#include "Engine/FileSystem/AssetID.h"

#include "Core/Testing/TestFramework.h"

using namespace sw;

SW_TEST_CASE( AssetIDTest, KeepsTheTextItWasGiven )
{
    const AssetID id{ "engine/shaders/lit.hlsl" };
    SW_EXPECT_FALSE( id.isEmpty() );
    SW_EXPECT_EQUAL( string_view( "engine/shaders/lit.hlsl" ), id.view() );
    SW_EXPECT_TRUE( AssetID{}.isEmpty() );
}
