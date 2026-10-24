#include "pch.h"

#include "TestFramework/TestContext.h"

namespace test
{
    void TestContext::begin( const sw::string& testName )
    {
        _testName = testName;
        _listFailure.clear();
        _listCleanup.clear();
        _listSkippedPart.clear();
        _missingFeature = BuildFeature::Count;
        _bSkipped       = false;
        _bTempPathUsed  = false;
        _bSeedUsed      = false;
    }

    void TestContext::deferCleanup( sw::Delegate<void()> cleanup )
    {
        _listCleanup.push_back( cleanup );
    }

    void TestContext::runCleanup()
    {
        for ( size_t index = _listCleanup.size(); index > 0; --index )
        {
            try
            {
                _listCleanup[index - 1]();
            }
            catch ( const std::exception& exception )
            {
                addFailure( { "cleanup", "TestFramework", 0, exception.what(), {} } );
            }
            catch ( ... )
            {
                addFailure( { "cleanup", "TestFramework", 0, "Unknown cleanup exception", {} } );
            }
        }
        _listCleanup.clear();
    }

    void TestContext::addFailure( const TestFailure& failure )
    {
        _listFailure.push_back( failure );
    }
} // namespace test
