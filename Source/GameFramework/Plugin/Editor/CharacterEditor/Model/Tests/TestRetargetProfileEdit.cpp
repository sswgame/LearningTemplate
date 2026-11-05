#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileEdit.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( RetargetProfileEditTest, NamesRoundTripThroughText )
{
    const vector<hashed_string> listName = RetargetProfileEdit::parseNames( "spine, chest,neck" );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listName.size() ) );
    SW_EXPECT_TRUE( listName[1] == hashed_string( "chest" ) );
    const vector<hashed_string> listAgain = RetargetProfileEdit::parseNames( RetargetProfileEdit::joinNames( listName ) );
    SW_EXPECT_TRUE( listAgain == listName );
}

// 빈 이름 · 겹친 이름은 다시 읽을 때 오류라 더하지 않는다.
SW_TEST_CASE( RetargetProfileEditTest, ChainNamesMustBeNewAndNotEmpty )
{
    vector<RetargetChain> listChain;
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddChain( listChain, hashed_string{} ) );
    SW_EXPECT_TRUE( RetargetProfileEdit::canAddChain( listChain, hashed_string( "Arm" ) ) );
    RetargetChain chain{};
    chain._name = hashed_string( "Arm" );
    listChain.push_back( chain );
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddChain( listChain, hashed_string( "Arm" ) ) );
}

SW_TEST_CASE( RetargetProfileEditTest, APoseBoneIsListedOnce )
{
    vector<RetargetPoseBone> listPose;
    SW_EXPECT_TRUE( RetargetProfileEdit::canAddPoseBone( listPose, hashed_string( "hip" ) ) );
    listPose.push_back( RetargetPoseBone{ hashed_string( "hip" ), float3{} } );
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddPoseBone( listPose, hashed_string( "hip" ) ) );
}
