#include "TestFramework/TestFramework.h"

#include "Core/Container/StringUtil.h"
#include "Core/Process/StandardOutput.h"
#include "Core/Time/MonotonicClock.h"

#include <algorithm>
#include <cmath>

namespace sw::test
{
    namespace
    {
        struct TestFrameworkInternal
        {
            /** @brief 등록표 저장소입니다. 정적 초기화 순서 때문에 함수 안 정적이다. */
            static vector<const TestCase*>& getCaseList()
            {
                static vector<const TestCase*> s_listCase;
                return s_listCase;
            }

            /** @brief 이번 케이스의 실패 수입니다(케이스는 한 스레드에서 차례로 돈다). */
            inline static uint32 s_failureCount = 0;

            static void writeLine( string_view text )
            {
                (void)StandardOutput::write( text );
                (void)StandardOutput::write( "\n" );
            }

            static string makeCaseName( const TestCase& testCase )
            {
                string name( testCase._pSuite );
                name += '.';
                name += testCase._pName;
                return name;
            }
        };
    } // namespace
} // namespace sw::test

namespace sw::test
{
    void TestRegistry::add( const TestCase* pCase )
    {
        TestFrameworkInternal::getCaseList().push_back( pCase );
    }

    const vector<const TestCase*>& TestRegistry::getAll()
    {
        return TestFrameworkInternal::getCaseList();
    }

    void TestContext::reportFailure( const utf8* pFile, int32 line, string_view message )
    {
        ++TestFrameworkInternal::s_failureCount;
        utf8 arrBuffer[constant::kMaxBuffer2048];
        formatstring( arrBuffer, constant::kMaxBuffer2048, "%#(%#): error: %#", pFile, line, message );
        TestFrameworkInternal::writeLine( arrBuffer );
    }

    uint32 TestContext::getFailureCount()
    {
        return TestFrameworkInternal::s_failureCount;
    }

    void TestContext::beginCase()
    {
        TestFrameworkInternal::s_failureCount = 0;
    }

    bool checkNear( float64 expected, float64 actual, float64 tolerance, const utf8* pExpression, const utf8* pFile, int32 line )
    {
        // NaN 은 어떤 비교도 거짓이라 `<=` 가 거짓 — 통과로 새지 않는다.
        if ( std::fabs( expected - actual ) <= tolerance )
            return true;
        utf8 arrBuffer[constant::kMaxBuffer1024];
        formatstring( arrBuffer, constant::kMaxBuffer1024, "%# — expected %#, actual %#, tolerance %#", pExpression, expected, actual, tolerance );
        TestContext::reportFailure( pFile, line, arrBuffer );
        return false;
    }

    bool checkCondition( bool bPassed, const utf8* pExpression, const utf8* pFile, int32 line )
    {
        if ( bPassed == false )
            TestContext::reportFailure( pFile, line, pExpression );
        return bPassed;
    }

    bool TestRunner::parseArguments( int32 argumentCount, const char* const* ppArgument, TestRunOptions& outOptions, string& outError )
    {
        for ( int32 argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex )
        {
            const string_view argument( ppArgument[argumentIndex] );
            if ( StringUtil::startsWith( argument, "--filter=" ) )
                outOptions._filter = string( argument.substr( 9 ) );
            else if ( StringUtil::startsWith( argument, "--gtest_filter=" ) ) // IDE 테스트 탐색기가 gtest 형식으로 부른다
                outOptions._filter = string( argument.substr( 15 ) );
            else if ( argument == "--list" || argument == "--gtest_list_tests" )
                outOptions._bListOnly = true;
            else
            {
                outError = string( "unknown argument: " ) + string( argument ) + " (accepted: --filter=<wildcards>, --list)";
                return false;
            }
        }
        return true;
    }

    bool TestRunner::matchesFilter( string_view filter, string_view name )
    {
        // 쉼표로 가른 패턴 — 앞에 `-` 가 붙은 것은 빼기다. 더하기 패턴이 하나도 없으면 모두를 더한 것으로 본다.
        bool   bIncluded    = false;
        bool   bHasPositive = false;
        size_t start        = 0;
        while ( start <= filter.size() )
        {
            const size_t      end     = std::min( filter.find( ',', start ), filter.size() );
            const string_view pattern = filter.substr( start, end - start );
            if ( pattern.empty() == false )
            {
                if ( pattern.front() == '-' )
                {
                    if ( StringUtil::matchesWildcard( pattern.substr( 1 ), name ) )
                        return false;
                }
                else
                {
                    bHasPositive = true;
                    bIncluded    = bIncluded || StringUtil::matchesWildcard( pattern, name );
                }
            }
            start = end + 1;
        }
        return bHasPositive ? bIncluded : true;
    }

    int32 TestRunner::run( const TestRunOptions& options )
    {
        vector<const TestCase*> listCase;
        for ( const TestCase* pCase : TestRegistry::getAll() )
        {
            if ( matchesFilter( options._filter, TestFrameworkInternal::makeCaseName( *pCase ) ) )
                listCase.push_back( pCase );
        }
        // 등록 순서는 링크 순서다 — 이름 순으로 돌려 실행마다 같은 순서를 지킨다.
        std::sort( listCase.begin(), listCase.end(), []( const TestCase* pA, const TestCase* pB )
        {
            const int32 suiteOrder = StringUtil::compare( pA->_pSuite, pB->_pSuite );
            return suiteOrder != 0 ? suiteOrder < 0 : StringUtil::compare( pA->_pName, pB->_pName ) < 0;
        } );

        if ( options._bListOnly )
        {
            for ( const TestCase* pCase : listCase )
            {
                TestFrameworkInternal::writeLine( TestFrameworkInternal::makeCaseName( *pCase ) );
            }
            return 0;
        }
        if ( listCase.empty() )
        {
            TestFrameworkInternal::writeLine( string( "[  NO TESTS ] no case matches the filter '" ) + options._filter + "'" );
            return 2;
        }

        utf8           arrBuffer[constant::kMaxBuffer1024];
        vector<string> listFailedName;
        const int64    runStart = MonotonicClock::nowNanoseconds();
        for ( const TestCase* pCase : listCase )
        {
            const string name = TestFrameworkInternal::makeCaseName( *pCase );
            TestFrameworkInternal::writeLine( string( "[ RUN      ] " ) + name );
            TestContext::beginCase();
            const int64 caseStart = MonotonicClock::nowNanoseconds();
            pCase->_pfnRun();
            const int64 caseMilliseconds = ( MonotonicClock::nowNanoseconds() - caseStart ) / constant::kNanosecondsPerMillisecond;
            const bool  bPassed          = TestContext::getFailureCount() == 0;
            formatstring( arrBuffer, constant::kMaxBuffer1024, "%# %# (%# ms)", bPassed ? "[       OK ]" : "[  FAILED  ]", name.c_str(), caseMilliseconds );
            TestFrameworkInternal::writeLine( arrBuffer );
            if ( bPassed == false )
                listFailedName.push_back( name );
        }
        const int64 runMilliseconds = ( MonotonicClock::nowNanoseconds() - runStart ) / constant::kNanosecondsPerMillisecond;
        formatstring( arrBuffer, constant::kMaxBuffer1024, "[==========] %# cases ran (%# ms), %# passed, %# failed", static_cast<uint32>( listCase.size() ), runMilliseconds,
                      static_cast<uint32>( listCase.size() - listFailedName.size() ), static_cast<uint32>( listFailedName.size() ) );
        TestFrameworkInternal::writeLine( arrBuffer );
        for ( const string& failedName : listFailedName )
        {
            TestFrameworkInternal::writeLine( string( "[  FAILED  ] " ) + failedName );
        }
        return listFailedName.empty() ? 0 : 1;
    }
} // namespace sw::test
