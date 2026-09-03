#include "pch.h"

#include "TestFramework/TestContext.h"

namespace test
{
    void TestContext::begin( const sw::string& testName )
    {
        _testName = testName;
        _listFailure.clear();
        _listCleanup.clear();
        _skipReason.clear();
        _bSkipped = false;
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
                addFailure( "cleanup", "TestFramework", 0, exception.what() );
            }
            catch ( ... )
            {
                addFailure( "cleanup", "TestFramework", 0, "Unknown cleanup exception" );
            }
        }
        _listCleanup.clear();
    }

    void TestContext::addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message )
    {
        _listFailure.push_back( { condition, file, line, message } );
    }

    void TestContext::skip( const sw::string& reason, const sw::string& file, int32 line )
    {
        _bSkipped   = true;
        _skipReason = reason;
        (void)file;
        (void)line;
    }
} // namespace test
