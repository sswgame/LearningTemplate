#include "pch.h"

#include "TestFramework/TestAssert.h"

#include "TestFramework/TestRegistry.h"

namespace test
{
    void reportFailure( const utf8* pCondition, const utf8* pFile, int32 line, const utf8* pMessage )
    {
        TestRegistry::getInstance().addFailure( pCondition, pFile, line, pMessage != nullptr ? pMessage : "" );
    }

    void reportFailure( const utf8* pCondition, const utf8* pFile, int32 line, const sw::string& message )
    {
        TestRegistry::getInstance().addFailure( pCondition, pFile, line, message );
    }

    void expectSameText( const utf8* pCondition, const utf8* pFile, int32 line, const ComparableText& expected, const ComparableText& actual )
    {
        if ( expected._bNull == actual._bNull && expected._text == actual._text )
            return;

        std::ostringstream oss;
        oss << "Expected [" << ( expected._bNull ? sw::string_view( "<null>" ) : expected._text ) << "], Actual ["
            << ( actual._bNull ? sw::string_view( "<null>" ) : actual._text ) << "]";
        reportFailure( pCondition, pFile, line, oss.str().c_str() );
    }
} // namespace test
