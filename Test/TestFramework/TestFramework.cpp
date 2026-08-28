#include "pch.h"

#include "TestFramework/TestFramework.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Scene/SceneManager.h"

namespace test
{
	namespace
	{
		/** @brief CLI 인자 값의 따옴표를 제거합니다. */
		std::string_view trimArgValue( std::string_view value )
		{
			while ( value.empty() == false && ( value.front() == '"' || value.front() == '\'' ) )
				value.remove_prefix( 1 );
			while ( value.empty() == false && ( value.back() == '"' || value.back() == '\'' ) )
				value.remove_suffix( 1 );
			return value;
		}

	} // namespace

	TestRegistry& TestRegistry::getInstance()
	{
		static TestRegistry s_instance;
		return s_instance;
	}

	void TestRegistry::registerTest( const sw::string& suiteName, const sw::string& testName, sw::Delegate<void()> func )
	{
		_listTest.push_back( { suiteName, testName, func } );
	}

	void TestRegistry::setFilter( const sw::string& filter )
	{
		_listIncludePattern.clear();
		_listExcludePattern.clear();

		if ( filter.empty() )
			return;

		size_t start{ 0 };
		while ( start <= filter.size() )
		{
			const size_t comma = filter.find( ',', start );
			sw::string	 token = filter.substr( start, comma == sw::string::npos ? sw::string::npos : comma - start );
			while ( token.empty() == false && token.front() == ' ' )
				token.erase( token.begin() );
			while ( token.empty() == false && token.back() == ' ' )
				token.pop_back();

			if ( token.empty() == false )
			{
				if ( token.front() == '-' )
				{
					_listExcludePattern.push_back( token.substr( 1 ) );
				}
				else
				{
					_listIncludePattern.push_back( token );
				}
			}

			if ( comma == sw::string::npos )
				break;
			start = comma + 1;
		}
	}

	void TestRegistry::configureFromArgs( int32 argc, utf8* argv[] )
	{
		for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
		{
			const std::string_view arg = argv[argIndex] != nullptr ? argv[argIndex] : "";
			if ( arg == "--test_list" || arg == "--gtest_list_tests" )
			{
				_listOnly = true;
				continue;
			}

			constexpr std::string_view kFilterPrefixA = "--test_filter=";
			constexpr std::string_view kFilterPrefixB = "--gtest_filter=";
			if ( arg.substr( 0, kFilterPrefixA.size() ) == kFilterPrefixA )
			{
				setFilter( sw::string( trimArgValue( arg.substr( kFilterPrefixA.size() ) ) ) );
				continue;
			}
			if ( arg.substr( 0, kFilterPrefixB.size() ) == kFilterPrefixB )
			{
				setFilter( sw::string( trimArgValue( arg.substr( kFilterPrefixB.size() ) ) ) );
				continue;
			}

			if ( arg == "--test_filter" || arg == "--gtest_filter" )
			{
				if ( argIndex + 1 < argc && argv[argIndex + 1] != nullptr )
				{
					setFilter( sw::string( trimArgValue( argv[++argIndex] ) ) );
				}
			}
		}
	}

	bool TestRegistry::matchGlob( const sw::string& pattern, const sw::string& text )
	{
		if ( pattern.empty() || pattern == "*" )
			return true;

		size_t		 textPos{ 0 };
		size_t		 patternPos{ 0 };
		const size_t textSize	 = text.size();
		const size_t patternSize = pattern.size();

		size_t starPattern = sw::string::npos;
		size_t starText	   = sw::string::npos;

		while ( textPos < textSize )
		{
			if ( patternPos < patternSize && pattern[patternPos] == '*' )
			{
				starPattern = patternPos++;
				starText	= textPos;
				continue;
			}

			if ( patternPos < patternSize && pattern[patternPos] == text[textPos] )
			{
				++patternPos;
				++textPos;
				continue;
			}

			if ( starPattern != sw::string::npos )
			{
				patternPos = starPattern + 1;
				textPos	   = ++starText;
				continue;
			}

			return false;
		}

		while ( patternPos < patternSize && pattern[patternPos] == '*' )
			++patternPos;

		return patternPos == patternSize;
	}

	bool TestRegistry::matchesFilter( const sw::string& fullName ) const
	{
		bool included = _listIncludePattern.empty();
		for ( const sw::string& pattern : _listIncludePattern )
		{
			if ( matchGlob( pattern, fullName ) )
			{
				included = true;
				break;
			}
		}

		if ( included == false )
			return false;

		for ( const sw::string& pattern : _listExcludePattern )
		{
			if ( matchGlob( pattern, fullName ) )
				return false;
		}

		return true;
	}

	void TestRegistry::addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message )
	{
		_currentTestFailed = true;
		std::fprintf( stdout, "\n  [FAILED] %s:%d\n    Condition: %s\n", file.c_str(), line, condition.c_str() );
		if ( message.empty() == false )
		{
			std::fprintf( stdout, "    Message  : %s\n", message.c_str() );
		}
		std::fflush( stdout );
		SW_LOG_ERROR( "\n  [FAILED] %#:%#", file.c_str(), line );
		SW_LOG_ERROR( "    Condition: %#", condition.c_str() );
		if ( message.empty() == false )
		{
			SW_LOG_ERROR( "    Message  : %#", message.c_str() );
		}
	}

	void TestRegistry::skipCurrentTest( [[maybe_unused]] const sw::string& reason, [[maybe_unused]] const sw::string& file, [[maybe_unused]] int32 line )
	{
		_currentTestSkipped = true;
		SW_LOG_INFO( "\n  [SKIPPED] %#:%# — %#", file.c_str(), line, reason.c_str() );
	}

	void TestRegistry::listTests() const
	{
		std::fprintf( stdout, "Registered tests (%u):\n", static_cast<uint32>( _listTest.size() ) );
		SW_LOG_INFO( "Registered tests (%#):", static_cast<uint32>( _listTest.size() ) );
		for ( const TestCaseInfo& testInfo : _listTest )
		{
			std::fprintf( stdout, "  %s\n", testInfo.fullName().c_str() );
			SW_LOG_INFO( "  %#", testInfo.fullName().c_str() );
		}
		std::fflush( stdout );
	}

	int32 TestRegistry::runAllTests()
	{
		if ( _listOnly )
		{
			listTests();
			return 0;
		}

		int32	passedCount{ 0 };
		int32	failedCount{ 0 };
		int32	skippedCount{ 0 };
		float64 totalMs{ 0.0 };

		uint32 runnableCount{ 0 };
		for ( const TestCaseInfo& testInfo : _listTest )
		{
			if ( matchesFilter( testInfo.fullName() ) )
				++runnableCount;
		}

		const uint32 filteredOut = static_cast<uint32>( _listTest.size() ) - runnableCount;

		SW_LOG_INFO( "====================================================" );
		SW_LOG_INFO( " Running %# / %# Test Cases...", runnableCount, static_cast<uint32>( _listTest.size() ) );
		if ( filteredOut > 0 )
		{
			SW_LOG_INFO( " Filtered out: %#", filteredOut );
		}
		SW_LOG_INFO( "====================================================" );

		sw::vector<sw::string> failedTestNames;

		for ( const TestCaseInfo& testInfo : _listTest )
		{
			if ( matchesFilter( testInfo.fullName() ) == false )
				continue;

			_currentTestFailed	= false;
			_currentTestSkipped = false;
			std::fprintf( stdout, "[ RUN      ] %s\n", testInfo.fullName().c_str() );
			std::fflush( stdout );
			SW_LOG_INFO( "%#", testInfo.fullName().c_str() );

			const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

			testInfo._func();

			if ( sw::engine::areEngineServicesBound() )
			{
				sw::engine::getSceneManager().cancelPendingAsyncLoads();
				sw::engine::getTaskManager().clear();
			}

			const std::chrono::high_resolution_clock::time_point end	 = std::chrono::high_resolution_clock::now();
			const float64										 elapsed = std::chrono::duration<float64, std::milli>( end - start ).count();
			totalMs += elapsed;

			if ( _currentTestSkipped && _currentTestFailed == false )
			{
				++skippedCount;
				std::fprintf( stdout, "[  SKIPPED ] %s\n", testInfo.fullName().c_str() );
				std::fflush( stdout );
				SW_LOG_INFO( "%#", testInfo.fullName().c_str() );
			}
			else if ( _currentTestFailed )
			{
				++failedCount;
				failedTestNames.push_back( testInfo.fullName() );
				std::fprintf( stdout, "[  FAILED  ] %s (%.2f ms)\n", testInfo.fullName().c_str(), elapsed );
				std::fflush( stdout );
				SW_LOG_ERROR( "%# (%.2f ms)", testInfo.fullName().c_str(), elapsed );
			}
			else
			{
				++passedCount;
				std::fprintf( stdout, "[       OK ] %s (%.2f ms)\n", testInfo.fullName().c_str(), elapsed );
				std::fflush( stdout );
				SW_LOG_INFO( "%# (%.2f ms)", testInfo.fullName().c_str(), elapsed );
			}
		}

		SW_LOG_INFO( "====================================================" );
		SW_LOG_INFO( " Test Summary: %# Passed, %# Failed, %# Skipped (%# ms total)",
					 passedCount,
					 failedCount,
					 skippedCount,
					 totalMs );
		SW_LOG_INFO( "====================================================" );

		std::fprintf( stdout, "====================================================\n" );
		std::fprintf( stdout, " Tests passed: %d / %d (%d skipped, %.2f ms total)\n", passedCount, passedCount + failedCount, skippedCount, totalMs );
		if ( failedCount > 0 )
		{
			std::fprintf( stdout, " Tests failed (%d):\n", failedCount );
			for ( const auto& name : failedTestNames )
			{
				std::fprintf( stdout, "   - %s\n", name.c_str() );
			}
		}
		std::fprintf( stdout, "====================================================\n" );
		std::fflush( stdout );

		return ( failedCount == 0 ) ? 0 : 1;
	}
} // namespace test
