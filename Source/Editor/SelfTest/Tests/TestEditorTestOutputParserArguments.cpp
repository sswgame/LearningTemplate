#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/SelfTest/EditorTestOutputParser.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( EditorTestOutputParserTest, EditorScenariosTurnTheEditorOn )
{
    const vector<string> listEditor = EditorTestOutputParser::makeScenarioArguments( "engine/automation/editor/x.scenario.xml", "dx12" );
    SW_EXPECT_TRUE( listEditor[0] == "-dx12" );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( listEditor.size() ) );
    const vector<string> listGame = EditorTestOutputParser::makeScenarioArguments( "game/a/automation/x.scenario.xml", {} );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listGame.size() ) );
}
