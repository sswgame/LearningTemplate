#include "pch.h"

#include "TestFramework/TestTempPath.h"

#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestAssert.h"
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

namespace test
{
    ScopedTempResourceRoot::ScopedTempResourceRoot()
    {
        // 처음 부르는 케이스면 루트를 먼저 찾아 둔다 — 검색 우선순위가 비어 있으면 교체가 검색 폴더를 다시 짓지 않는다.
        if ( sw::ResourcePaths::initialize() == false )
            reportFailure( "ResourcePaths::initialize()", __FILE__, __LINE__, "리소스 루트를 찾지 못해 임시 루트로 바꿀 수 없다" );
        _rootFolderPath       = sw::FileUtil::normalizeSeparators( makeTempDirectory( "Resource" ) );
        _sourceRootFolderPath = sw::ResourcePaths::exchangeRootFolderPath( _rootFolderPath );
    }

    ScopedTempResourceRoot::~ScopedTempResourceRoot()
    {
        (void)sw::ResourcePaths::exchangeRootFolderPath( _sourceRootFolderPath ); // 앞 루트는 이미 쓰던 것이라 되돌림이 실패할 일이 없다
    }

    sw::string ScopedTempResourceRoot::makePath( sw::string_view relativePath ) const
    {
        const bool bOutside = relativePath.empty() || sw::FileUtil::isAbsolutePath( relativePath ) || sw::ResourcePaths::hasParentDirectoryComponent( relativePath );
        if ( bOutside )
        {
            reportFailure( "ScopedTempResourceRoot::makePath", __FILE__, __LINE__,
                           sw::string( "임시 루트 밖 경로다 — 루트 기준 상대 경로만 받는다: " ) + sw::string( relativePath ) );
            return {};
        }
        return sw::FileUtil::normalizeSeparators( sw::FileUtil::joinPath( _rootFolderPath, relativePath ) );
    }

    bool ScopedTempResourceRoot::writeTextFile( sw::string_view relativePath, sw::string_view text ) const
    {
        const sw::string path = makePath( relativePath );
        if ( path.empty() )
            return false;
        sw::FileUtil::ensureParentDirectoryExists( path );
        return sw::FileUtil::writeTextFile( path, text );
    }
} // namespace test
