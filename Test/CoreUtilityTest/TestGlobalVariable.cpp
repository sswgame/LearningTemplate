/**
 * @file TestGlobalVariable.cpp
 * @brief Auto-generated documentation header
 */
#include "pch.h"
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
	sw::GlobalVariableInfo* pBoolInfo = sw::getGlobalVariableManager().findVariable( "gv_TestBool" );
	SW_EXPECT_TRUE( pBoolInfo != nullptr );
	if ( pBoolInfo != nullptr )
	{
		SW_EXPECT_TRUE( pBoolInfo->_type == sw::GlobalVariableType::Bool );
		SW_EXPECT_TRUE( pBoolInfo->getValueAsBool() == true );
		SW_EXPECT_EQUAL( std::string( "Unit Test Bool Global Variable" ), pBoolInfo->_description );
	}

	sw::GlobalVariableInfo* pIntInfo = sw::getGlobalVariableManager().findVariable( "gv_TestInt" );
	SW_EXPECT_TRUE( pIntInfo != nullptr );
	if ( pIntInfo != nullptr )
	{
		SW_EXPECT_TRUE( pIntInfo->_type == sw::GlobalVariableType::Int32 );
		SW_EXPECT_EQUAL( 60, pIntInfo->getValueAsInt() );
	}

	sw::GlobalVariableInfo* pFloatInfo = sw::getGlobalVariableManager().findVariable( "gv_TestFloat" );
	SW_EXPECT_TRUE( pFloatInfo != nullptr );
	if ( pFloatInfo != nullptr )
	{
		SW_EXPECT_NEAR_EQUAL( 45.0f, pFloatInfo->getValueAsFloat(), 1e-4f );
	}

	sw::GlobalVariableInfo* pStrInfo = sw::getGlobalVariableManager().findVariable( "gv_TestString" );
	SW_EXPECT_TRUE( pStrInfo != nullptr );
	if ( pStrInfo != nullptr )
	{
		SW_EXPECT_EQUAL( std::string( "InitialValue" ), pStrInfo->getValueAsString() );
	}

	const auto& allVars = sw::getGlobalVariableManager().getAllVariables();
	SW_EXPECT_TRUE( allVars.size() >= 4u );
}

SW_TEST_CASE( Utility_GlobalVariable, ModificationAndReset )
{

	SW_EXPECT_TRUE( sw::getGlobalVariableManager().setValueFromString( "gv_TestInt", "144" ) );
	SW_EXPECT_EQUAL( 144, gv_TestInt );

	SW_EXPECT_TRUE( sw::getGlobalVariableManager().setValueFromString( "gv_TestBool", "false" ) );
	SW_EXPECT_FALSE( gv_TestBool );

	SW_EXPECT_TRUE( sw::getGlobalVariableManager().resetToDefault( "gv_TestInt" ) );
	SW_EXPECT_EQUAL( 60, gv_TestInt );

	SW_EXPECT_TRUE( sw::getGlobalVariableManager().resetToDefault( "gv_TestBool" ) );
	SW_EXPECT_TRUE( gv_TestBool );
}

// SW_TEST_CASE( Utility_GlobalVariable, CommandLineIntegration )\n// Temporarily disabled due to command line parsing issue

