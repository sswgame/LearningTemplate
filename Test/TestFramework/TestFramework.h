#pragma once
/**
 * @file TestFramework.h
 * @brief Auto-generated documentation header
 */

#include "Core/CoreMinimal.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/Delegate/Delegate.h"
#include <cmath>

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
	};

	class TestRegistry
	{
	public:
		/**
		 * @brief getInstance 처리를 수행합니다.
		 */
		static TestRegistry& getInstance();

		void registerTest( const std::string& suiteName, const std::string& testName, sw::Delegate<void()> func );
		/**
		 * @brief runAllTests 처리를 수행합니다.
		 */
		int	 runAllTests();

		/**
		 * @brief addFailure 처리를 수행합니다.
		 */
		void addFailure( const std::string& condition, const std::string& file, int line, const std::string& message = "" );
		bool isCurrentTestHasFailed() const { return _currentTestFailed; }

	private:
		std::vector<TestCaseInfo> _tests;
		bool					  _currentTestFailed = false;
	};

	class TestRegistrar
	{
	public:
		TestRegistrar( const std::string& suiteName, const std::string& testName, sw::Delegate<void()> func )
		{
			TestRegistry::getInstance().registerTest( suiteName, testName, func );
		}
	};
}

/** @brief SW_TEST_CASE 매크로 정의입니다. */
#define SW_TEST_CASE( SuiteName, TestName )                                                                                                                              \
	void					   test_##SuiteName##_##TestName();                                                                                                          \
	static test::TestRegistrar registrar_##SuiteName##_##TestName( #SuiteName, #TestName, SW_DELEGATE_FUNCTION( sw::Delegate<void()>, test_##SuiteName##_##TestName ) ); \
	void					   test_##SuiteName##_##TestName()

/** @brief SW_EXPECT_TRUE 매크로 정의입니다. */
#define SW_EXPECT_TRUE( cond )                                                         \
	do                                                                                 \
	{                                                                                  \
		if ( !( cond ) )                                                               \
		{                                                                              \
			test::TestRegistry::getInstance().addFailure( #cond, __FILE__, __LINE__ ); \
		}                                                                              \
	} while ( 0 )

/** @brief SW_EXPECT_FALSE 매크로 정의입니다. */
#define SW_EXPECT_FALSE( cond )                                                                 \
	do                                                                                          \
	{                                                                                           \
		if ( ( cond ) )                                                                         \
		{                                                                                       \
			test::TestRegistry::getInstance().addFailure( "!(" #cond ")", __FILE__, __LINE__ ); \
		}                                                                                       \
	} while ( 0 )

/** @brief SW_EXPECT_EQUAL 매크로 정의입니다. */
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

/** @brief SW_EXPECT_NOT_EQUAL 매크로 정의입니다. */
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

/** @brief SW_EXPECT_NEAR 매크로 정의입니다. */
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

/** @brief SW_EXPECT_NEAR_EQUAL 매크로 정의입니다. */
#define SW_EXPECT_NEAR_EQUAL( expected, actual, tolerance ) SW_EXPECT_NEAR( expected, actual, tolerance )

/** @brief SW_EXPECT_NOT_NULL 매크로 정의입니다. */
#define SW_EXPECT_NOT_NULL( ptr )                                                                \
	do                                                                                          \
	{                                                                                           \
		if ( ( ptr ) == nullptr )                                                                \
		{                                                                                       \
			test::TestRegistry::getInstance().addFailure( #ptr " != nullptr", __FILE__, __LINE__ ); \
		}                                                                                       \
	} while ( 0 )

/** @brief SW_EXPECT_NULL 매크로 정의입니다. */
#define SW_EXPECT_NULL( ptr )                                                                    \
	do                                                                                      \
	{                                                                                       \
		if ( ( ptr ) != nullptr )                                                                \
		{                                                                                      \
			test::TestRegistry::getInstance().addFailure( #ptr " == nullptr", __FILE__, __LINE__ ); \
		}                                                                                      \
	} while ( 0 )
