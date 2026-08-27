#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"

#include "TestFramework/TestFramework.h"

SW_GLOBAL_VARIABLE_BOOL( gv_testBool, true, "Unit Test Bool Global Variable" );
SW_GLOBAL_VARIABLE_INT( gv_testInt, 60, "Unit Test Int32 Global Variable" );
SW_GLOBAL_VARIABLE_FLOAT( gv_testFloat, 45.0f, "Unit Test Float Global Variable" );
SW_GLOBAL_VARIABLE_STRING( gv_testString, "InitialValue", "Unit Test String Global Variable" );

SW_EXTERN_GLOBAL_VARIABLE_BOOL( gv_testBool );
SW_EXTERN_GLOBAL_VARIABLE_INT( gv_testInt );
SW_EXTERN_GLOBAL_VARIABLE_FLOAT( gv_testFloat );
SW_EXTERN_GLOBAL_VARIABLE_STRING( gv_testString );

// ------------------------------------------------------------------------------
// 1) Engine_GlobalVariable — 등록·수정·커맨드라인
// ------------------------------------------------------------------------------
/**
 * @brief [Engine_GlobalVariable] 등록
 */
SW_TEST_CASE( Engine_GlobalVariable, Registration )
{
	sw::GlobalVariableInfo* pBoolInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testBool" );
	SW_EXPECT_TRUE( pBoolInfo != nullptr );
	if ( pBoolInfo != nullptr )
	{
		SW_EXPECT_TRUE( pBoolInfo->_type == sw::GlobalVariableType::Boolean );
		SW_EXPECT_TRUE( pBoolInfo->getValueAsBool() );
		SW_EXPECT_EQUAL( sw::string( "Unit Test Bool Global Variable" ), pBoolInfo->_description );
	}

	sw::GlobalVariableInfo* pIntInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testInt" );
	SW_EXPECT_TRUE( pIntInfo != nullptr );
	if ( pIntInfo != nullptr )
	{
		SW_EXPECT_TRUE( pIntInfo->_type == sw::GlobalVariableType::Int32 );
		SW_EXPECT_EQUAL( 60, pIntInfo->getValueAsInt() );
	}

	sw::GlobalVariableInfo* pFloatInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testFloat" );
	SW_EXPECT_TRUE( pFloatInfo != nullptr );
	if ( pFloatInfo != nullptr )
	{
		SW_EXPECT_NEAR_EQUAL( 45.0f, pFloatInfo->getValueAsFloat(), 1e-4f );
	}

	sw::GlobalVariableInfo* pStrInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testString" );
	SW_EXPECT_TRUE( pStrInfo != nullptr );
	if ( pStrInfo != nullptr )
	{
		SW_EXPECT_EQUAL( sw::string( "InitialValue" ), pStrInfo->getValueAsString() );
	}

	const uint32 varCount = sw::engine::getGlobalVariableManager().getVariableCount();
	SW_EXPECT_TRUE( varCount >= 4u );
}

/**
 * @brief [Engine_GlobalVariable] 수정과 리셋
 */
SW_TEST_CASE( Engine_GlobalVariable, ModificationAndReset )
{

	SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_testInt", "144" ) );
	SW_EXPECT_EQUAL( 144, gv_testInt );

	SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_testBool", "false" ) );
	SW_EXPECT_FALSE( gv_testBool );

	SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().resetToDefault( "gv_testInt" ) );
	SW_EXPECT_EQUAL( 60, gv_testInt );

	SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().resetToDefault( "gv_testBool" ) );
	SW_EXPECT_TRUE( gv_testBool );
}

/**
 * @brief [Engine_GlobalVariable] 커맨드라인 연동
 */
SW_TEST_CASE( Engine_GlobalVariable, CommandLineIntegration )
{
	// 부분 CommandLineManager 에서 GlobalVariableManager::updateFromCommandLine 을 쓰지 않는다.
	// CLI 맵에 GV 이름이 없으면 getArgument 가 assert 한다(과거 flake/abort).
	// 테스트 대상 변수만 파싱한 뒤 setValueFromString 으로 적용한다.
	sw::engine::getGlobalVariableManager().resetToDefault( "gv_testInt" );
	sw::engine::getGlobalVariableManager().resetToDefault( "gv_testString" );

	sw::CommandLineManager cmd;
	cmd.initialize();
	cmd.addArgument<int32>( { "gv_testInt" }, true, int32{ 60 }, false );
	cmd.addArgument<sw::string>( { "gv_testString" }, true, sw::string( "InitialValue" ), false );

	utf8  arg0[] = "CoreUtilityTest";
	utf8  arg1[] = "gv_testInt=777";
	utf8  arg2[] = "gv_testString=FromCLI";
	utf8* argv[] = { reinterpret_cast<utf8*>( arg0 ), reinterpret_cast<utf8*>( arg1 ), reinterpret_cast<utf8*>( arg2 ) };
	cmd.parse( 3, argv );

	int32 parsedInt{ 0 };
	SW_EXPECT_TRUE( cmd.getArgument( "gv_testInt", parsedInt ) );
	SW_EXPECT_EQUAL( 777, parsedInt );

	sw::string parsedStr;
	SW_EXPECT_TRUE( cmd.getArgument( "gv_testString", parsedStr ) );
	SW_EXPECT_EQUAL( sw::string( "FromCLI" ), parsedStr );

	SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_testInt", sw::to_string( parsedInt ) ) );
	SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_testString", parsedStr ) );
	SW_EXPECT_EQUAL( 777, gv_testInt );
	SW_EXPECT_EQUAL( sw::string( "FromCLI" ), gv_testString );

	sw::engine::getGlobalVariableManager().resetToDefault( "gv_testInt" );
	sw::engine::getGlobalVariableManager().resetToDefault( "gv_testString" );
}

/**
 * @brief [Engine_GlobalVariable] 미등록 변수 조회 및 안전성 검증
 */
SW_TEST_CASE( Engine_GlobalVariable, NonExistentVariableHandling )
{
	sw::GlobalVariableInfo* pMissing = sw::engine::getGlobalVariableManager().findVariable( "gv_nonExistentVariable" );
	SW_EXPECT_NULL( pMissing );

	SW_EXPECT_FALSE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_nonExistentVariable", "123" ) );
	SW_EXPECT_FALSE( sw::engine::getGlobalVariableManager().resetToDefault( "gv_nonExistentVariable" ) );
}
