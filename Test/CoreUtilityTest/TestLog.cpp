/**
 * @file TestLog.cpp
 * @brief Auto-generated documentation header
 */
#include "Core/CoreMinimal.h"

#include "TestFramework.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/File/FileUtil.h"

SW_TEST_CASE( Utility_Log, LoggerInitialized )
{
	const std::string& folderPath = sw::Logger::getLogFolderPath();
	SW_EXPECT_FALSE( folderPath.empty() );
}

SW_TEST_CASE( Utility_Log, LogFolderExists )
{
	const std::string& folderPath = sw::Logger::getLogFolderPath();
	if ( folderPath.empty() == false )
	{
		SW_EXPECT_TRUE( sw::FileUtil::isDirectoryExist( folderPath ) );
	}
}

SW_TEST_CASE( Utility_Log, WriteLogDoesNotCrash )
{
	SW_LOG_INFO( "[Test] Info level log" );
	SW_LOG_WARNING( "[Test] Warning level log" );
	SW_LOG_ERROR( "[Test] Error level log" );
	SW_LOG_TRACE( "[Test] Trace level log" );
	SW_EXPECT_TRUE( true );
}

SW_TEST_CASE( Utility_Log, LogLevelCount )
{
	constexpr uint32 expected = 4u;
	SW_EXPECT_EQUAL( expected, static_cast<uint32>( sw::Logger::LogLevel::Count ) );
}

SW_TEST_CASE( Utility_Log, LogMacrosExecution )
{
	SW_LOG_INFO( "Testing SW_LOG_INFO %#", 123 );
	SW_LOG_WARNING( "Testing SW_LOG_WARNING %#", "warning" );
	SW_LOG_ERROR( "Testing SW_LOG_ERROR %#", 404 );
	SW_LOG_TRACE( "Testing SW_LOG_TRACE %#", 3.14f );
	SW_EXPECT_TRUE( true );
}
