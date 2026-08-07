/**
 * @file TestFramework.cpp
 * @brief Test registry, filter matching, and runner
 */
#include "TestFramework.h"
#include "Core/Utility/Log/Logger.h"

namespace test
{
	namespace
	{
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

	void TestRegistry::registerTest( const std::string& suiteName, const std::string& testName, sw::Delegate<void()> func )
	{
		_tests.push_back( { suiteName, testName, func } );
	}

	void TestRegistry::setFilter( const std::string& filter )
	{
		_includePatterns.clear();
		_excludePatterns.clear();

		if ( filter.empty() )
			return;

		size_t start = 0;
		while ( start <= filter.size() )
		{
			const size_t comma = filter.find( ',', start );
			std::string	 token = filter.substr( start, comma == std::string::npos ? std::string::npos : comma - start );
			while ( token.empty() == false && token.front() == ' ' )
				token.erase( token.begin() );
			while ( token.empty() == false && token.back() == ' ' )
				token.pop_back();

			if ( token.empty() == false )
			{
				if ( token.front() == '-' )
				{
					_excludePatterns.push_back( token.substr( 1 ) );
				}
				else
				{
					_includePatterns.push_back( token );
				}
			}

			if ( comma == std::string::npos )
				break;
			start = comma + 1;
		}
	}

	void TestRegistry::configureFromArgs( int argc, char* argv[] )
	{
		for ( int i = 1; i < argc; ++i )
		{
			const std::string_view arg = argv[i] != nullptr ? argv[i] : "";
			if ( arg == "--test_list" || arg == "--gtest_list_tests" )
			{
				_listOnly = true;
				continue;
			}

			constexpr std::string_view kFilterPrefixA = "--test_filter=";
			constexpr std::string_view kFilterPrefixB = "--gtest_filter=";
			if ( arg.substr( 0, kFilterPrefixA.size() ) == kFilterPrefixA )
			{
				setFilter( std::string( trimArgValue( arg.substr( kFilterPrefixA.size() ) ) ) );
				continue;
			}
			if ( arg.substr( 0, kFilterPrefixB.size() ) == kFilterPrefixB )
			{
				setFilter( std::string( trimArgValue( arg.substr( kFilterPrefixB.size() ) ) ) );
				continue;
			}

			if ( arg == "--test_filter" || arg == "--gtest_filter" )
			{
				if ( i + 1 < argc && argv[i + 1] != nullptr )
				{
					setFilter( std::string( trimArgValue( argv[++i] ) ) );
				}
			}
		}
	}

	bool TestRegistry::matchGlob( const std::string& pattern, const std::string& text )
	{
		if ( pattern.empty() || pattern == "*" )
			return true;

		size_t		 textPos	= 0;
		size_t		 patternPos = 0;
		const size_t textSize	= text.size();
		const size_t patternSize = pattern.size();

		size_t starPattern = std::string::npos;
		size_t starText	   = std::string::npos;

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

			if ( starPattern != std::string::npos )
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

	bool TestRegistry::matchesFilter( const std::string& fullName ) const
	{
		bool included = _includePatterns.empty();
		for ( const std::string& pattern : _includePatterns )
		{
			if ( matchGlob( pattern, fullName ) )
			{
				included = true;
				break;
			}
		}

		if ( included == false )
			return false;

		for ( const std::string& pattern : _excludePatterns )
		{
			if ( matchGlob( pattern, fullName ) )
				return false;
		}

		return true;
	}

	void TestRegistry::addFailure( const std::string& condition, const std::string& file, int line, const std::string& message )
	{
		_currentTestFailed = true;
		SW_LOG_ERROR( "\n  [FAILED] %#:%#", file.c_str(), line );
		SW_LOG_ERROR( "    Condition: %#", condition.c_str() );
		if ( message.empty() == false )
		{
			SW_LOG_ERROR( "    Message  : %#", message.c_str() );
		}
	}

	void TestRegistry::skipCurrentTest( const std::string& reason, const std::string& file, int line )
	{
		_currentTestSkipped = true;
		SW_LOG_INFO( "\n  [SKIPPED] %#:%# — %#", file.c_str(), line, reason.c_str() );
	}

	void TestRegistry::listTests() const
	{
		SW_LOG_INFO( "Registered tests (%#):", static_cast<uint32>( _tests.size() ) );
		for ( const TestCaseInfo& testInfo : _tests )
		{
			SW_LOG_INFO( "  %#", testInfo.fullName().c_str() );
		}
	}

	int TestRegistry::runAllTests()
	{
		if ( _listOnly )
		{
			listTests();
			return 0;
		}

		int	   passedCount	= 0;
		int	   failedCount	= 0;
		int	   skippedCount = 0;
		int	   filteredOut	= 0;
		double totalMs		= 0.0;

		uint32 runnableCount = 0;
		for ( const TestCaseInfo& testInfo : _tests )
		{
			if ( matchesFilter( testInfo.fullName() ) )
				++runnableCount;
			else
				++filteredOut;
		}

		SW_LOG_INFO( "====================================================" );
		SW_LOG_INFO( " Running %# / %# Test Cases...", runnableCount, static_cast<uint32>( _tests.size() ) );
		if ( filteredOut > 0 )
		{
			SW_LOG_INFO( " Filtered out: %#", filteredOut );
		}
		SW_LOG_INFO( "====================================================" );

		for ( const TestCaseInfo& testInfo : _tests )
		{
			if ( matchesFilter( testInfo.fullName() ) == false )
				continue;

			_currentTestFailed	= false;
			_currentTestSkipped = false;
			SW_LOG_INFO( "[ RUN      ] %#", testInfo.fullName().c_str() );

			const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

			try
			{
				testInfo._func();
			}
			catch ( const TestSkipException& )
			{
				// skipCurrentTest already logged the reason
			}
			catch ( const TestAssertException& )
			{
				// failure already recorded by SW_ASSERT_*
			}
			catch ( const std::exception& e )
			{
				addFailure( "Unhandled Exception", __FILE__, __LINE__, e.what() );
			}
			catch ( ... )
			{
				addFailure( "Unknown Exception", __FILE__, __LINE__ );
			}

			const std::chrono::high_resolution_clock::time_point end	 = std::chrono::high_resolution_clock::now();
			const double										 elapsed = std::chrono::duration<double, std::milli>( end - start ).count();
			totalMs += elapsed;

			if ( _currentTestSkipped && _currentTestFailed == false )
			{
				++skippedCount;
				SW_LOG_INFO( "[  SKIPPED ] %# (%# ms)", testInfo.fullName().c_str(), elapsed );
			}
			else if ( _currentTestFailed )
			{
				++failedCount;
				SW_LOG_ERROR( "[  FAILED  ] %# (%# ms)", testInfo.fullName().c_str(), elapsed );
			}
			else
			{
				++passedCount;
				SW_LOG_INFO( "[       OK ] %# (%# ms)", testInfo.fullName().c_str(), elapsed );
			}
		}

		SW_LOG_INFO( "====================================================" );
		SW_LOG_INFO( " Test Summary: %# Passed, %# Failed, %# Skipped (%# ms total)",
					 passedCount,
					 failedCount,
					 skippedCount,
					 totalMs );
		SW_LOG_INFO( "====================================================" );

		return ( failedCount == 0 ) ? 0 : 1;
	}
} // namespace test
