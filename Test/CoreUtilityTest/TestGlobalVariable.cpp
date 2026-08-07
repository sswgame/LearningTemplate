/**
 * @file TestGlobalVariable.cpp
 * @brief Auto-generated documentation header
 */
#include "pch.h"
#include "TestFramework/TestModuleHeads.h"
#include "TestFramework.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"

SW_GLOBAL_VARIABLE_BOOL( gv_TestBool, true, "Unit Test Bool Global Variable" );
SW_GLOBAL_VARIABLE_INT( gv_TestInt, 60, "Unit Test Int32 Global Variable" );
SW_GLOBAL_VARIABLE_FLOAT( gv_TestFloat, 45.0f, "Unit Test Float Global Variable" );
SW_GLOBAL_VARIABLE_STRING( gv_TestString, "InitialValue", "Unit Test String Global Variable" );

SW_EXTERN_GLOBAL_VARIABLE_BOOL( gv_TestBool );
SW_EXTERN_GLOBAL_VARIABLE_INT( gv_TestInt );
SW_EXTERN_GLOBAL_VARIABLE_FLOAT( gv_TestFloat );
SW_EXTERN_GLOBAL_VARIABLE_STRING( gv_TestString );

SW_TEST_CASE( Utility_GlobalVariable, Registration )
{
	sw::GlobalVariableInfo* pBoolInfo = sw::core::getGlobalVariableManager().findVariable( "gv_TestBool" );
	SW_EXPECT_TRUE( pBoolInfo != nullptr );
	if ( pBoolInfo != nullptr )
	{
		SW_EXPECT_TRUE( pBoolInfo->_type == sw::GlobalVariableType::Bool );
		SW_EXPECT_TRUE( pBoolInfo->getValueAsBool() == true );
		SW_EXPECT_EQUAL( std::string( "Unit Test Bool Global Variable" ), pBoolInfo->_description );
	}

	sw::GlobalVariableInfo* pIntInfo = sw::core::getGlobalVariableManager().findVariable( "gv_TestInt" );
	SW_EXPECT_TRUE( pIntInfo != nullptr );
	if ( pIntInfo != nullptr )
	{
		SW_EXPECT_TRUE( pIntInfo->_type == sw::GlobalVariableType::Int32 );
		SW_EXPECT_EQUAL( 60, pIntInfo->getValueAsInt() );
	}

	sw::GlobalVariableInfo* pFloatInfo = sw::core::getGlobalVariableManager().findVariable( "gv_TestFloat" );
	SW_EXPECT_TRUE( pFloatInfo != nullptr );
	if ( pFloatInfo != nullptr )
	{
		SW_EXPECT_NEAR_EQUAL( 45.0f, pFloatInfo->getValueAsFloat(), 1e-4f );
	}

	sw::GlobalVariableInfo* pStrInfo = sw::core::getGlobalVariableManager().findVariable( "gv_TestString" );
	SW_EXPECT_TRUE( pStrInfo != nullptr );
	if ( pStrInfo != nullptr )
	{
		SW_EXPECT_EQUAL( std::string( "InitialValue" ), pStrInfo->getValueAsString() );
	}

	const auto& allVars = sw::core::getGlobalVariableManager().getAllVariables();
	SW_EXPECT_TRUE( allVars.size() >= 4u );
}

SW_TEST_CASE( Utility_GlobalVariable, ModificationAndReset )
{

	SW_EXPECT_TRUE( sw::core::getGlobalVariableManager().setValueFromString( "gv_TestInt", "144" ) );
	SW_EXPECT_EQUAL( 144, gv_TestInt );

	SW_EXPECT_TRUE( sw::core::getGlobalVariableManager().setValueFromString( "gv_TestBool", "false" ) );
	SW_EXPECT_FALSE( gv_TestBool );

	SW_EXPECT_TRUE( sw::core::getGlobalVariableManager().resetToDefault( "gv_TestInt" ) );
	SW_EXPECT_EQUAL( 60, gv_TestInt );

	SW_EXPECT_TRUE( sw::core::getGlobalVariableManager().resetToDefault( "gv_TestBool" ) );
	SW_EXPECT_TRUE( gv_TestBool );
}

SW_TEST_CASE( Utility_GlobalVariable, CommandLineIntegration )
{
	// Avoid GlobalVariableManager::updateFromCommandLine on a partial CommandLineManager:
	// getArgument asserts when a GV name is missing from the CLI map (historical flake/abort).
	// Parse only the vars under test, then apply via setValueFromString.
	sw::core::getGlobalVariableManager().resetToDefault( "gv_TestInt" );
	sw::core::getGlobalVariableManager().resetToDefault( "gv_TestString" );

	sw::CommandLineManager cmd;
	cmd.initialize();
	cmd.addArgument<int32>( { "gv_TestInt" }, true, int32{ 60 }, false );
	cmd.addArgument<std::string>( { "gv_TestString" }, true, std::string( "InitialValue" ), false );

	char  arg0[] = "CoreUtilityTest";
	char  arg1[] = "gv_TestInt=777";
	char  arg2[] = "gv_TestString=FromCLI";
	utf8* argv[] = { reinterpret_cast<utf8*>( arg0 ), reinterpret_cast<utf8*>( arg1 ), reinterpret_cast<utf8*>( arg2 ) };
	cmd.parse( 3, argv );

	int32 parsedInt = 0;
	SW_EXPECT_TRUE( cmd.getArgument( "gv_TestInt", parsedInt ) );
	SW_EXPECT_EQUAL( 777, parsedInt );

	std::string parsedStr;
	SW_EXPECT_TRUE( cmd.getArgument( "gv_TestString", parsedStr ) );
	SW_EXPECT_EQUAL( std::string( "FromCLI" ), parsedStr );

	SW_EXPECT_TRUE( sw::core::getGlobalVariableManager().setValueFromString( "gv_TestInt", std::to_string( parsedInt ) ) );
	SW_EXPECT_TRUE( sw::core::getGlobalVariableManager().setValueFromString( "gv_TestString", parsedStr ) );
	SW_EXPECT_EQUAL( 777, gv_TestInt );
	SW_EXPECT_EQUAL( std::string( "FromCLI" ), gv_TestString );

	sw::core::getGlobalVariableManager().resetToDefault( "gv_TestInt" );
	sw::core::getGlobalVariableManager().resetToDefault( "gv_TestString" );
}
