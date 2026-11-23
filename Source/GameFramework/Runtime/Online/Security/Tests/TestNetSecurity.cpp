// NetSecurityTest — TLS 컨텍스트 도우미: 설정 경로 풀기, 짝 없는 인증서 · 키와 없는 파일의 거절, 이름도 지문도 없는 클라이언트를 만들지 않는 것(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Security/OpenSSL/OpenSSLNetSecurityProvider.h"

using namespace sw;

namespace
{
    /** @brief 자체 서명 인증서와 키를 테스트 폴더에 쓴 것입니다. */
    struct NetSecurityTestFiles
    {
        OpenSSLNetSecurityProvider _provider{};
        AbsolutePath               _folder;
        string                     _certificateFile{};
        string                     _privateKeyFile{};
        string                     _garbageFile{};

        explicit NetSecurityTestFiles( string_view caseName )
            : _folder{ test::makeScratchPath( caseName ) }
        {
            string     certificatePem;
            string     privateKeyPem;
            const bool bMade = FileUtil::ensureDirectoryExists( _folder ) && _provider.createSelfSignedCertificate( "localhost", 1, certificatePem, privateKeyPem );
            SW_EXPECT_TRUE( bMade );
            _certificateFile = writeFile( "server.crt", certificatePem );
            _privateKeyFile  = writeFile( "server.key", privateKeyPem );
            _garbageFile     = writeFile( "garbage.pem", "this is not a certificate" );
        }

        string writeFile( string_view name, const string& text ) const
        {
            const AbsolutePath path = _folder.join( name );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( path, text ) );
            return string( path.str() );
        }
    };

    template <typename T>
    bool hasErrorKind( const expected<T>& result, ErrorKind kind )
    {
        return result.has_value() == false && result.error().kind() == kind;
    }
} // namespace

/** @brief [NetSecurityTest] 빈 경로는 "정하지 않음"이라 빈 글이고, 상대 경로는 프로젝트 아래이며, 드라이브 상대는 오류다(빈 글로 바꾸면 개발 인증서로 물러난다) */
SW_TEST_CASE( NetSecurityTest, ResolvesProjectPaths )
{
    const expected<string> unset = NetSecurity::resolveProjectPath( "" );
    SW_REQUIRE_TRUE( unset.has_value() );
    SW_EXPECT_TRUE( unset->empty() );
    const expected<string> relative = NetSecurity::resolveProjectPath( "Certificates/server.crt" );
    SW_REQUIRE_TRUE( relative.has_value() );
    const string& project = ProcessRoots::get()._project.str();
    SW_EXPECT_TRUE( relative->substr( 0, project.size() ) == project && relative->find( "server.crt" ) != string::npos );
    SW_EXPECT_TRUE( hasErrorKind( NetSecurity::resolveProjectPath( "C:server.crt" ), ErrorKind::InvalidArgument ) );
}

/** @brief [NetSecurityTest] 서버는 짝이 맞는 인증서 · 키로만 선다. 하나만 있거나 없는 파일 · 인증서가 아닌 파일이면 오류이고 개발 인증서로 물러나지 않는다 */
SW_TEST_CASE( NetSecurityTest, ServerNeedsACertificatePair )
{
    NetSecurityTestFiles files( "net_security_server" );
    SW_EXPECT_TRUE( NetSecurity::createServerTLSContext( files._provider, files._certificateFile, files._privateKeyFile, "" ).has_value() );
    SW_EXPECT_TRUE( hasErrorKind( NetSecurity::createServerTLSContext( files._provider, files._certificateFile, "", "" ), ErrorKind::InvalidArgument ) );
    SW_EXPECT_TRUE( hasErrorKind( NetSecurity::createServerTLSContext( files._provider, "", files._privateKeyFile, "" ), ErrorKind::InvalidArgument ) );
    const string missing = string( files._folder.join( "missing.key" ).str() );
    SW_EXPECT_TRUE( hasErrorKind( NetSecurity::createServerTLSContext( files._provider, files._certificateFile, missing, "" ), ErrorKind::NotFound ) );
    SW_EXPECT_FALSE( NetSecurity::createServerTLSContext( files._provider, files._garbageFile, files._privateKeyFile, "" ).has_value() );
}

/** @brief [NetSecurityTest] 이름 없는 클라이언트는 믿을 인증서의 지문을 고정해야 하고, 인증서가 아닌 파일이면 만들지 않는다 — 이름도 지문도 없이 검증을 건너뛰지 않는다 */
SW_TEST_CASE( NetSecurityTest, ClientNeedsANameOrAPinnedCertificate )
{
    NetSecurityTestFiles files( "net_security_client" );
    SW_EXPECT_TRUE( NetSecurity::createClientTLSContext( files._provider, files._certificateFile, "localhost" ).has_value() );
    SW_EXPECT_TRUE( NetSecurity::createClientTLSContext( files._provider, files._certificateFile, "" ).has_value() );
    SW_EXPECT_TRUE( hasErrorKind( NetSecurity::createClientTLSContext( files._provider, files._garbageFile, "" ), ErrorKind::InvalidArgument ) );
    const string missing = string( files._folder.join( "missing.pem" ).str() );
    SW_EXPECT_TRUE( hasErrorKind( NetSecurity::createClientTLSContext( files._provider, missing, "localhost" ), ErrorKind::NotFound ) );
}
