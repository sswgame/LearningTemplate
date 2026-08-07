#pragma once
/**
 * @file TestFramework.h
 * @brief Lightweight auto-registering unit test framework for Core
 */

#include "Core/CoreMinimal.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/Delegate/Delegate.h"
#include <cmath>
#include <exception>

namespace test
{
	struct TestFailure
	{
		std::string _condition;
		std::string _file;
		int			_line;
		std::string _message;
	};

	struct TestCaseInfo
	{
		std::string			 _groupName;
		std::string			 _testName;
		sw::Delegate<void()> _func;

		std::string fullName() const { return _groupName + "." + _testName; }
	};

	/** @brief Thrown by SW_TEST_SKIP to leave the current case early. */
	class TestSkipException : public std::exception
	{
	public:
		explicit TestSkipException( std::string reason )
			: _reason{ std::move( reason ) }
		{
		}

		const char* what() const noexcept override { return _reason.c_str(); }

	private:
		std::string _reason;
	};

	/** @brief Thrown by SW_ASSERT_* after recording a failure. */
	class TestAssertException : public std::exception
	{
	public:
		const char* what() const noexcept override { return "Test assertion failed"; }
	};

	class TestRegistry
	{
	public:
		static TestRegistry& getInstance();

		void registerTest( const std::string& suiteName, const std::string& testName, sw::Delegate<void()> func );

		/** @brief Parse --test_filter / --gtest_filter / --test_list from argv. */
		void configureFromArgs( int argc, char* argv[] );

		/** @brief Glob filter: "Suite.*", "Utility_*", comma includes, "-RHI*" excludes. */
		void setFilter( const std::string& filter );

		int	 runAllTests();
		void listTests() const;

		void addFailure( const std::string& condition, const std::string& file, int line, const std::string& message = "" );
		void skipCurrentTest( const std::string& reason, const std::string& file, int line );

		bool isCurrentTestHasFailed() const { return _currentTestFailed; }
		bool isCurrentTestSkipped() const { return _currentTestSkipped; }

	private:
		bool matchesFilter( const std::string& fullName ) const;
		static bool matchGlob( const std::string& pattern, const std::string& text );

		std::vector<TestCaseInfo> _tests;
		std::vector<std::string>  _includePatterns;
		std::vector<std::string>  _excludePatterns;
		bool					  _listOnly			  = false;
		bool					  _currentTestFailed  = false;
		bool					  _currentTestSkipped = false;
	};

	class TestRegistrar
	{
	public:
		TestRegistrar( const std::string& suiteName, const std::string& testName, sw::Delegate<void()> func )
		{
			TestRegistry::getInstance().registerTest( suiteName, testName, func );
		}
	};
} // namespace test

/** @brief Registers and defines a test case. */
#define SW_TEST_CASE( SuiteName, TestName )                                                                                                                              \
	void					   test_##SuiteName##_##TestName();                                                                                                          \
	static test::TestRegistrar registrar_##SuiteName##_##TestName( #SuiteName, #TestName, SW_DELEGATE_FUNCTION( sw::Delegate<void()>, test_##SuiteName##_##TestName ) ); \
	void					   test_##SuiteName##_##TestName()

/** @brief Skip the current test with a reason (does not fail the suite). */
#define SW_TEST_SKIP( reason )                                                               \
	do                                                                                       \
	{                                                                                        \
		test::TestRegistry::getInstance().skipCurrentTest( ( reason ), __FILE__, __LINE__ ); \
		return;                                                                              \
	} while ( 0 )

/** @brief Soft expectation — records failure and continues. */
#define SW_EXPECT_TRUE( cond )                                                         \
	do                                                                                 \
	{                                                                                  \
		if ( !( cond ) )                                                               \
		{                                                                              \
			test::TestRegistry::getInstance().addFailure( #cond, __FILE__, __LINE__ ); \
		}                                                                              \
	} while ( 0 )

#define SW_EXPECT_TRUE_MSG( cond, msg )                                                      \
	do                                                                                       \
	{                                                                                        \
		if ( !( cond ) )                                                                     \
		{                                                                                    \
			test::TestRegistry::getInstance().addFailure( #cond, __FILE__, __LINE__, msg ); \
		}                                                                                    \
	} while ( 0 )

#define SW_EXPECT_FALSE( cond )                                                                 \
	do                                                                                          \
	{                                                                                           \
		if ( ( cond ) )                                                                         \
		{                                                                                       \
			test::TestRegistry::getInstance().addFailure( "!(" #cond ")", __FILE__, __LINE__ ); \
		}                                                                                       \
	} while ( 0 )

#define SW_EXPECT_FALSE_MSG( cond, msg )                                                              \
	do                                                                                                \
	{                                                                                                 \
		if ( ( cond ) )                                                                               \
		{                                                                                             \
			test::TestRegistry::getInstance().addFailure( "!(" #cond ")", __FILE__, __LINE__, msg ); \
		}                                                                                             \
	} while ( 0 )

#define SW_EXPECT_EQUAL( expected, actual )                                                                          \
	do                                                                                                               \
	{                                                                                                                \
		if ( !( ( expected ) == ( actual ) ) )                                                                       \
		{                                                                                                            \
			std::ostringstream oss;                                                                                  \
			oss << "Expected [" << ( expected ) << "], Actual [" << ( actual ) << "]";                               \
			test::TestRegistry::getInstance().addFailure( #actual " == " #expected, __FILE__, __LINE__, oss.str() ); \
		}                                                                                                            \
	} while ( 0 )

#define SW_EXPECT_NOT_EQUAL( expected, actual )                                                                      \
	do                                                                                                               \
	{                                                                                                                \
		if ( ( expected ) == ( actual ) )                                                                            \
		{                                                                                                            \
			std::ostringstream oss;                                                                                  \
			oss << "Expected not equal to [" << ( expected ) << "]";                                                 \
			test::TestRegistry::getInstance().addFailure( #actual " != " #expected, __FILE__, __LINE__, oss.str() ); \
		}                                                                                                            \
	} while ( 0 )

#define SW_EXPECT_NEAR( expected, actual, tolerance )                                                                                      \
	do                                                                                                                                     \
	{                                                                                                                                      \
		if ( std::abs( ( expected ) - ( actual ) ) > ( tolerance ) )                                                                       \
		{                                                                                                                                  \
			std::ostringstream oss;                                                                                                        \
			oss << "Diff [" << std::abs( ( expected ) - ( actual ) ) << "] exceeds tolerance [" << ( tolerance ) << "]";                   \
			test::TestRegistry::getInstance().addFailure( "|" #actual " - " #expected "| <= " #tolerance, __FILE__, __LINE__, oss.str() ); \
		}                                                                                                                                  \
	} while ( 0 )

#define SW_EXPECT_NEAR_EQUAL( expected, actual, tolerance ) SW_EXPECT_NEAR( expected, actual, tolerance )

#define SW_EXPECT_NOT_NULL( ptr )                                                                   \
	do                                                                                              \
	{                                                                                               \
		if ( ( ptr ) == nullptr )                                                                   \
		{                                                                                           \
			test::TestRegistry::getInstance().addFailure( #ptr " != nullptr", __FILE__, __LINE__ ); \
		}                                                                                           \
	} while ( 0 )

#define SW_EXPECT_NULL( ptr )                                                                       \
	do                                                                                              \
	{                                                                                               \
		if ( ( ptr ) != nullptr )                                                                   \
		{                                                                                           \
			test::TestRegistry::getInstance().addFailure( #ptr " == nullptr", __FILE__, __LINE__ ); \
		}                                                                                           \
	} while ( 0 )

/** @brief Compare null-terminated / string-like values via std::string. */
#define SW_EXPECT_STREQ( expected, actual )                                                                              \
	do                                                                                                                   \
	{                                                                                                                    \
		const std::string _sw_expect_streq_e( expected );                                                                \
		const std::string _sw_expect_streq_a( actual );                                                                  \
		if ( _sw_expect_streq_e != _sw_expect_streq_a )                                                                  \
		{                                                                                                                \
			std::ostringstream oss;                                                                                      \
			oss << "Expected [" << _sw_expect_streq_e << "], Actual [" << _sw_expect_streq_a << "]";                     \
			test::TestRegistry::getInstance().addFailure( #actual " == " #expected, __FILE__, __LINE__, oss.str() );     \
		}                                                                                                                \
	} while ( 0 )

#define SW_EXPECT_EMPTY( value )                                                                         \
	do                                                                                                   \
	{                                                                                                    \
		if ( !( ( value ).empty() ) )                                                                    \
		{                                                                                                \
			test::TestRegistry::getInstance().addFailure( #value ".empty()", __FILE__, __LINE__ );       \
		}                                                                                                \
	} while ( 0 )

/** @brief Hard assertion — records failure and aborts the current test case. */
#define SW_ASSERT_TRUE( cond )                                                         \
	do                                                                                 \
	{                                                                                  \
		if ( !( cond ) )                                                               \
		{                                                                              \
			test::TestRegistry::getInstance().addFailure( #cond, __FILE__, __LINE__ ); \
			throw test::TestAssertException();                                         \
		}                                                                              \
	} while ( 0 )

#define SW_ASSERT_FALSE( cond )                                                                 \
	do                                                                                          \
	{                                                                                           \
		if ( ( cond ) )                                                                         \
		{                                                                                       \
			test::TestRegistry::getInstance().addFailure( "!(" #cond ")", __FILE__, __LINE__ ); \
			throw test::TestAssertException();                                                  \
		}                                                                                       \
	} while ( 0 )

#define SW_ASSERT_EQUAL( expected, actual )                                                                          \
	do                                                                                                               \
	{                                                                                                                \
		if ( !( ( expected ) == ( actual ) ) )                                                                       \
		{                                                                                                            \
			std::ostringstream oss;                                                                                  \
			oss << "Expected [" << ( expected ) << "], Actual [" << ( actual ) << "]";                               \
			test::TestRegistry::getInstance().addFailure( #actual " == " #expected, __FILE__, __LINE__, oss.str() ); \
			throw test::TestAssertException();                                                                       \
		}                                                                                                            \
	} while ( 0 )

#define SW_ASSERT_NOT_NULL( ptr )                                                                   \
	do                                                                                              \
	{                                                                                               \
		if ( ( ptr ) == nullptr )                                                                   \
		{                                                                                           \
			test::TestRegistry::getInstance().addFailure( #ptr " != nullptr", __FILE__, __LINE__ ); \
			throw test::TestAssertException();                                                      \
		}                                                                                           \
	} while ( 0 )
