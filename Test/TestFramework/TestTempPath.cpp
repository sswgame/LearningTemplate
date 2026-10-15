#include "pch.h"

#include "TestFramework/TestTempPath.h"

#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

#include "TestFramework/TestRegistry.h"

namespace test
{
    namespace
    {
        struct TestTempPathInternal
        {
            /** @brief 파일 이름에 넣어도 되는 글자만 남깁니다. */
            static sw::string toFileNameSafe( sw::string_view text )
            {
                sw::string safe;
                safe.reserve( text.size() );
                for ( const utf8 ch : text )
                {
                    const bool bSafe = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' ) || ( '0' <= ch && ch <= '9' );
                    safe.push_back( bSafe ? ch : '_' );
                }
                return safe;
            }
        };
    } // namespace
} // namespace test

namespace test
{
    sw::string getProcessTempDirectory()
    {
        sw::StringBuilder<sw::constant::kMaxBuffer64> name;
        name.append( "sw_" );
        name.append( static_cast<int32>( sw::Process::getCurrentProcessID() ) );
        return sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), name.c_str() );
    }

    sw::string getCaseTempDirectory( const sw::string& testName )
    {
        const sw::string processDirectory = getProcessTempDirectory();
        return testName.empty() ? processDirectory : sw::FileUtil::joinPath( processDirectory, TestTempPathInternal::toFileNameSafe( testName ) );
    }

    sw::string makeTempPath( sw::string_view fileName )
    {
        TestContext* pContext = TestRegistry::getInstance().getCurrentContext();
        if ( pContext->getTestName().empty() == false )
            pContext->markTempPathUsed();

        const sw::string caseDirectory = getCaseTempDirectory( pContext->getTestName() );
        sw::FileUtil::ensureDirectoryExists( caseDirectory );
        return sw::FileUtil::joinPath( caseDirectory, fileName );
    }

    sw::string makeTempDirectory( sw::string_view directoryName )
    {
        const sw::string directory = makeTempPath( directoryName );
        sw::FileUtil::ensureDirectoryExists( directory );
        return directory;
    }
} // namespace test
