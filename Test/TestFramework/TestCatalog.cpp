#include "pch.h"

#include "TestFramework/TestCatalog.h"

#include "Core/Console/DevCommandsEnabled.h"
#include "Core/Memory/MemoryTag.h"

#include "TestFramework/TestRegistry.h"

#include <cstdlib>

namespace test
{
    namespace
    {
        /** @brief 구성 기능 표 한 줄 — 이름과 이 빌드에 있는가. */
        struct BuildFeatureRow
        {
            const utf8* _pName;
            bool        _bAvailable;
        };

        struct TestCatalogInternal
        {
#if defined( SW_SHIPPING )
            static constexpr bool kDevelopmentBuild = false;
#else
            static constexpr bool kDevelopmentBuild = true;
#endif
#if defined( SW_DEBUG )
            static constexpr bool kDebugBuild = true;
#else
            static constexpr bool kDebugBuild = false;
#endif
#if defined( SW_PLATFORM_WINDOWS )
            static constexpr bool kWindows = true;
#else
            static constexpr bool kWindows = false;
#endif
#if defined( SW_PLATFORM_LINUX )
            static constexpr bool kLinux = true;
#else
            static constexpr bool kLinux = false;
#endif
#if defined( SW_WITH_CLIENT_CODE )
            static constexpr bool kClientCode = true;
#else
            static constexpr bool kClientCode = false;
#endif
#if defined( SW_SANITIZER_ADDRESS ) || defined( SW_SANITIZER_THREAD )
            static constexpr bool kFatalSignalHandling = false;
#else
            static constexpr bool kFatalSignalHandling = true;
#endif
#if defined( SW_ENABLE_STL_CONTAINER )
            static constexpr bool kSwContainerTags = false;
#else
            static constexpr bool kSwContainerTags = true;
#endif
            // 리눅스 개발 콘솔 오버레이(X11DevConsoleWindow)는 이 조건의 빌드에만 있다 — TestX11DevConsole.cpp 의 본문 조건과 같다.
#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )
            static constexpr bool kX11ConsoleOverlay = true;
#else
            static constexpr bool kX11ConsoleOverlay = false;
#endif
            /** @brief `BuildFeature` 와 같은 순서 — 기능 하나에 한 줄. */
            static constexpr BuildFeatureRow kArrBuildFeature[] = {
                {   "DevelopmentBuild",                kDevelopmentBuild},
                {       "DebugAsserts",                      kDebugBuild},
                {           "DebugLog",                      kDebugBuild},
                {    "MemoryTagScopes",      sw::kMemoryTagScopesEnabled},
                {    "SourceResources",                kDevelopmentBuild},
                {      "ShippingBuild",               !kDevelopmentBuild},
                {    "WindowsPlatform",                         kWindows},
                {      "LinuxPlatform",                           kLinux},
                {         "ClientCode",                      kClientCode},
                {        "AudioAssets", kDevelopmentBuild || kClientCode},
                {"FatalSignalHandling",             kFatalSignalHandling},
                {    "SwContainerTags",                 kSwContainerTags},
                {  "X11ConsoleOverlay",               kX11ConsoleOverlay},
            };
            static_assert( sizeof( kArrBuildFeature ) / sizeof( kArrBuildFeature[0] ) == static_cast<size_t>( BuildFeature::Count ),
                           "every BuildFeature needs a row in kArrBuildFeature" );
        };
    } // namespace
} // namespace test

namespace test
{
    bool isBuildFeatureAvailable( BuildFeature feature )
    {
        return TestCatalogInternal::kArrBuildFeature[static_cast<size_t>( feature )]._bAvailable;
    }

    const utf8* getBuildFeatureName( BuildFeature feature )
    {
        return TestCatalogInternal::kArrBuildFeature[static_cast<size_t>( feature )]._pName;
    }

    sw::string getEnvironmentValue( const utf8* pEnvironmentName )
    {
        const utf8* pValue = std::getenv( pEnvironmentName );
        return pValue == nullptr ? sw::string{} : sw::string( pValue );
    }

    void TestCatalog::addCase( const utf8* pSuiteName, const utf8* pTestName, sw::Delegate<void()> func )
    {
        _listCase.push_back( { pSuiteName, pTestName, func } );
    }

    void TestCatalog::addCaseGenerator( CaseGenerator generator )
    {
        _listGenerator.push_back( generator );
    }

    void TestCatalog::expandCaseGenerators()
    {
        if ( _bGeneratorsExpanded )
            return;
        _bGeneratorsExpanded = true;
        for ( const CaseGenerator generator : _listGenerator )
        {
            generator( *this );
        }
    }

    void TestCatalog::addSuiteKind( const utf8* pSuiteName, SuiteKind kind, const utf8* pReason )
    {
        _arrMapSuiteKindReason[static_cast<size_t>( kind )][pSuiteName] = pReason;
    }

    void TestCatalog::addEnvironmentSuite( const utf8* pSuiteName, const utf8* pEnvironmentName, const utf8* pReason )
    {
        _mapEnvironmentSuite[pSuiteName] = { pEnvironmentName, pReason };
    }

    void TestCatalog::addTimeBudget( const utf8* pSuiteName, float64 maxCaseMilliseconds, const utf8* pReason )
    {
        _mapTimeBudget[pSuiteName] = { maxCaseMilliseconds, pReason };
    }

    bool TestCatalog::isSuiteOfKind( const sw::string& suiteName, SuiteKind kind ) const
    {
        const sw::map<sw::string, sw::string>& mapReason = getSuiteKindReasons( kind );
        return mapReason.find( suiteName ) != mapReason.end();
    }

    bool TestCatalog::isEnvironmentMissing( const sw::string& suiteName ) const
    {
        const auto suiteIt = _mapEnvironmentSuite.find( suiteName );
        return suiteIt != _mapEnvironmentSuite.end() && getEnvironmentValue( suiteIt->second._environmentName.c_str() ).empty();
    }

    const TimeBudget* TestCatalog::findTimeBudget( const sw::string& suiteName ) const
    {
        const auto budgetIt = _mapTimeBudget.find( suiteName );
        return budgetIt != _mapTimeBudget.end() ? &budgetIt->second : nullptr;
    }

    bool TestCatalog::hasSuite( const sw::string& suiteName ) const
    {
        for ( const TestCaseInfo& testInfo : _listCase )
        {
            if ( testInfo._groupName == suiteName )
                return true;
        }
        return false;
    }

    int32 TestCatalog::countDeclarationMismatch() const
    {
        int32      mismatchCount{ 0 };
        const auto reportStale = [&mismatchCount]( const utf8* pMacro, const sw::string& suiteName )
        {
            ++mismatchCount;
            std::fprintf( stdout, " %s( %s ) names a suite with no cases in this executable\n", pMacro, suiteName.c_str() );
            SW_LOG_ERROR( "%#( %# ) names a suite with no cases in this executable", pMacro, suiteName.c_str() );
        };

        constexpr const utf8* kArrKindMacro[] = { "SW_TEST_REQUIRES_HOST", "SW_TEST_LOAD_SUITE" };
        static_assert( sizeof( kArrKindMacro ) / sizeof( kArrKindMacro[0] ) == static_cast<size_t>( SuiteKind::Count ), "every SuiteKind needs its macro name" );
        for ( size_t kindIndex = 0; kindIndex < static_cast<size_t>( SuiteKind::Count ); ++kindIndex )
        {
            for ( const auto& [suiteName, reason] : _arrMapSuiteKindReason[kindIndex] )
            {
                if ( hasSuite( suiteName ) == false )
                    reportStale( kArrKindMacro[kindIndex], suiteName );
            }
        }
        for ( const auto& [suiteName, environment] : _mapEnvironmentSuite )
        {
            if ( hasSuite( suiteName ) == false )
                reportStale( "SW_TEST_REQUIRES_ENVIRONMENT", suiteName );
        }
        for ( const auto& [suiteName, budget] : _mapTimeBudget )
        {
            if ( hasSuite( suiteName ) == false )
                reportStale( "SW_TEST_TIME_BUDGET", suiteName );
        }
        return mismatchCount;
    }

    TestRegistrar::TestRegistrar( const utf8* pSuiteName, const utf8* pTestName, sw::Delegate<void()> func )
    {
        TestRegistry::getInstance().getCatalog().addCase( pSuiteName, pTestName, func );
    }

    CaseGeneratorRegistrar::CaseGeneratorRegistrar( TestCatalog::CaseGenerator generator )
    {
        TestRegistry::getInstance().getCatalog().addCaseGenerator( generator );
    }

    SuiteKindRegistrar::SuiteKindRegistrar( const utf8* pSuiteName, SuiteKind kind, const utf8* pReason )
    {
        TestRegistry::getInstance().getCatalog().addSuiteKind( pSuiteName, kind, pReason );
    }

    EnvironmentSuiteRegistrar::EnvironmentSuiteRegistrar( const utf8* pSuiteName, const utf8* pEnvironmentName, const utf8* pReason )
    {
        TestRegistry::getInstance().getCatalog().addEnvironmentSuite( pSuiteName, pEnvironmentName, pReason );
    }

    TimeBudgetRegistrar::TimeBudgetRegistrar( const utf8* pSuiteName, float64 maxCaseMilliseconds, const utf8* pReason )
    {
        TestRegistry::getInstance().getCatalog().addTimeBudget( pSuiteName, maxCaseMilliseconds, pReason );
    }
} // namespace test
