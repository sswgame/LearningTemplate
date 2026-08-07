/**
 * @file TestFramework.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/Log/Logger.h"

namespace test
{
	TestRegistry& TestRegistry::getInstance()
	{
		static TestRegistry s_instance;
		return s_instance;
	}

	void TestRegistry::registerTest( const std::string& suiteName, const std::string& testName, sw::Delegate<void()> func )
	{
		_tests.push_back( { suiteName, testName, func } );
	}

	void TestRegistry::addFailure( const std::string& condition, const std::string& file, int line, const std::string& message )
	{
		_currentTestFailed = true;
		SW_LOG_ERROR( "\n  [FAILED] %#:%#", file.c_str(), line );
		SW_LOG_ERROR( "    Condition: %#", condition.c_str() );
		if ( !message.empty() )
		{
			SW_LOG_ERROR( "    Message  : %#", message.c_str() );
		}
	}

	int TestRegistry::runAllTests()
	{
		int	   passedCount = 0;
		int	   failedCount = 0;
		double totalMs	   = 0.0;

		SW_LOG_INFO( "====================================================" );
		SW_LOG_INFO( " Running %# Test Cases in Suite...", static_cast<uint32>( _tests.size() ) );
		SW_LOG_INFO( "====================================================" );

		for ( const auto& testInfo : _tests )
		{
			_currentTestFailed = false;
			SW_LOG_INFO( "[ RUN      ] %#.%#", testInfo._groupName.c_str(), testInfo._testName.c_str() );

			std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

			try
			{
				testInfo._func();
			}
			catch ( const std::exception& e )
			{
				addFailure( "Unhandled Exception", __FILE__, __LINE__, e.what() );
			}
			catch ( ... )
			{
				addFailure( "Unknown Exception", __FILE__, __LINE__ );
			}

			std::chrono::high_resolution_clock::time_point end	   = std::chrono::high_resolution_clock::now();
			double										   elapsed = std::chrono::duration<double, std::milli>( end - start ).count();
			totalMs += elapsed;

			if ( _currentTestFailed )
			{
				failedCount++;
				SW_LOG_ERROR( "[  FAILED  ] %#.%# (%# ms)", testInfo._groupName.c_str(), testInfo._testName.c_str(), elapsed );
			}
			else
			{
				passedCount++;
				SW_LOG_INFO( "[       OK ] %#.%# (%# ms)", testInfo._groupName.c_str(), testInfo._testName.c_str(), elapsed );
			}
		}

		SW_LOG_INFO( "====================================================" );
		SW_LOG_INFO( " Test Summary: %# Passed, %# Failed (%# ms total)", passedCount, failedCount, totalMs );
		SW_LOG_INFO( "====================================================" );

		return ( failedCount == 0 ) ? 0 : 1;
	}
}
