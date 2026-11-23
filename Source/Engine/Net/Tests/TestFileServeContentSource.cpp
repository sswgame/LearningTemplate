// FileServeContentSourceTest — 장치가 PC 의 팩 폴더를 HTTP 로 받아 읽는 소스(Fileserve): 구간으로 받아 거울에 두고, 도장이 같으면 다시 받지 않으며,
// 걸린 폴더 밖 · 서버 목록의 탈출 이름은 거절한다. 서버는 가짜 `IHTTPClient` 다(`Scripts/net/fileserve.py` 와 같은 끝점 · 글)(nogpu).
#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Net/FileServe/FileServeContentSource.h"
#include "Engine/Net/HTTPClient.h"

using namespace sw;

namespace
{
    /** @brief 메모리 파일들을 fileserve 끝점(`/stat` · `/list` · `/file`)으로 내주는 가짜입니다. 경로는 인코딩할 글자가 없는 이름만 쓴다. */
    class FakeFileServeClient final : public IHTTPClient
    {
    public:
        struct File
        {
            vector<uint8> _bytes;
            uint64        _modifiedNanos{ 1 };
        };

        unordered_map<string, File> _mapFile;
        vector<string>              _listExtraName; ///< `/list` 에 덧붙일 이름(서버가 보낸 나쁜 이름)
        uint32                      _requestCount{ 0 };

        HTTPResponse send( const HTTPRequest& request ) override
        {
            ++_requestCount;
            HTTPResponse      response;
            const string_view url      = request._url;
            const size_t      queryAt  = url.find( '?' );
            const string_view endpoint = url.substr( kBase.size(), queryAt - kBase.size() );
            const string      path     = findQuery( url, "path" );
            if ( endpoint == "/stat" )
            {
                const auto it = _mapFile.find( path );
                if ( path.empty() )
                    setBody( response, "folder" );
                else if ( it != _mapFile.end() )
                {
                    string text = "file ";
                    StringUtil::appendNumber( text, static_cast<uint64>( it->second._bytes.size() ) );
                    text += " ";
                    StringUtil::appendNumber( text, it->second._modifiedNanos );
                    setBody( response, text );
                }
                else
                    response._status = 404;
                return response;
            }
            if ( endpoint == "/list" )
            {
                StringBuilder<constant::buffer::kMax256> text;
                for ( const auto& [name, file] : _mapFile )
                {
                    text.appendFormat( "%#\n", name );
                }
                for ( const string& name : _listExtraName )
                {
                    text.appendFormat( "%#\n", name );
                }
                setBody( response, text.view() );
                return response;
            }
            const auto it     = _mapFile.find( path );
            uint64     offset = 0;
            uint64     size   = 0;
            if ( endpoint != "/file" || it == _mapFile.end() || StringUtil::parseUint64( findQuery( url, "offset" ), offset ) == false ||
                 StringUtil::parseUint64( findQuery( url, "size" ), size ) == false || offset + size > it->second._bytes.size() )
            {
                response._status = 404;
                return response;
            }
            response._status = 200;
            response._body.assign( it->second._bytes.begin() + static_cast<ptrdiff_t>( offset ), it->second._bytes.begin() + static_cast<ptrdiff_t>( offset + size ) );
            return response;
        }

        static constexpr string_view kBase{ "http://fileserve.test" };

    private:
        static void setBody( HTTPResponse& response, string_view text )
        {
            response._status = 200;
            response._body.assign( text.begin(), text.end() );
        }

        static string findQuery( string_view url, string_view key )
        {
            const size_t queryAt = url.find( '?' );
            if ( queryAt == string_view::npos )
                return {};
            for ( const string_view pair : StringUtil::split( url.substr( queryAt + 1 ), "&" ) )
            {
                if ( pair.size() > key.size() && pair.substr( 0, key.size() ) == key && pair[key.size()] == '=' )
                    return string( pair.substr( key.size() + 1 ) );
            }
            return {};
        }
    };

    vector<uint8> makePattern( size_t size )
    {
        vector<uint8> bytes( size );
        for ( size_t index = 0; index < size; ++index )
        {
            bytes[index] = static_cast<uint8>( ( index * 31 ) & 0xFF );
        }
        return bytes;
    }
} // namespace

/**
 * @brief [FileServeContentSourceTest] 2.5 MiB 팩을 1 MiB 구간 셋으로 받아 거울에 두고, 다음 실행(새 소스)은 도장이 같아 받지 않으며, PC 쪽 수정 시각이 바뀌면 다시 받는다.
 *        결함이면 기동마다 팩 전체를 다시 받거나(느림), 다시 구운 팩을 받지 않는다(옛 콘텐츠)
 */
SW_TEST_CASE( FileServeContentSourceTest, MirrorsInChunksAndReusesFreshStamp )
{
    FakeFileServeClient client;
    const vector<uint8> pattern                         = makePattern( FileServeContentSource::kChunkBytes * 2 + FileServeContentSource::kChunkBytes / 2 );
    client._mapFile["game.pack"]                        = FakeFileServeClient::File{ pattern, 7 };
    const AbsolutePath                       mountRoot  = test::makeScratchPath( "FileServe/Packs" );
    const AbsolutePath                       mirrorRoot = test::makeScratchPath( "FileServe/Mirror" );
    StringBuilder<constant::buffer::kMax512> packPathBuilder;
    packPathBuilder.appendFormat( "%#/game.pack", mountRoot.str() );
    const string packPath( packPathBuilder.view() );

    {
        const FileServeContentSource source{ client, FakeFileServeClient::kBase, mountRoot.str(), mirrorRoot.str() };
        SW_EXPECT_TRUE( source.isFile( packPath ) );
        SW_EXPECT_TRUE( source.isFolder( mountRoot.str() ) );
        vector<uint8> bytes;
        SW_REQUIRE_TRUE( source.readFile( packPath, bytes ) );
        SW_EXPECT_TRUE( bytes == pattern );
        SW_EXPECT_EQUAL( uint64{ 3 }, source.getReceivedChunkCount().get() );
        SW_EXPECT_TRUE( source.open( packPath ).isValid() ); // 이미 받은 거울 — 다시 받지 않는다
        SW_EXPECT_EQUAL( uint64{ 3 }, source.getReceivedChunkCount().get() );
    }
    {
        const FileServeContentSource source{ client, FakeFileServeClient::kBase, mountRoot.str(), mirrorRoot.str() };
        vector<uint8>                bytes;
        SW_REQUIRE_TRUE( source.readFile( packPath, bytes ) );
        SW_EXPECT_EQUAL( uint64{ 0 }, source.getReceivedChunkCount().get() );
    }
    client._mapFile["game.pack"]._modifiedNanos = 8;
    {
        const FileServeContentSource source{ client, FakeFileServeClient::kBase, mountRoot.str(), mirrorRoot.str() };
        vector<uint8>                bytes;
        SW_REQUIRE_TRUE( source.readFile( packPath, bytes ) );
        SW_EXPECT_EQUAL( uint64{ 3 }, source.getReceivedChunkCount().get() );
    }
}

/**
 * @brief [FileServeContentSourceTest] 걸린 폴더 밖 경로는 묻지도 않고 없는 파일이며, 서버 목록에 탈출 이름(`../x`)이 섞이면 그 목록을 거절한다.
 *        결함이면 장치가 팩 폴더 밖 경로를 서버에 묻거나, 서버 응답 하나로 거울 밖(앱 데이터)에 파일을 쓴다
 */
SW_TEST_CASE( FileServeContentSourceTest, RefusesPathsOutsideTheMountAndEscapingNames )
{
    FakeFileServeClient client;
    client._mapFile["a.pack"]          = FakeFileServeClient::File{ makePattern( 16 ), 1 };
    const AbsolutePath           mount = test::makeScratchPath( "FileServe/Packs" );
    const FileServeContentSource source{ client, FakeFileServeClient::kBase, mount.str(), test::makeScratchPath( "FileServe/Mirror" ).str() };

    SW_EXPECT_FALSE( source.isFile( test::makeScratchPath( "Elsewhere/a.pack" ).str() ) );
    StringBuilder<constant::buffer::kMax512> outsidePath;
    outsidePath.appendFormat( "%#x/a.pack", mount.str() );
    SW_EXPECT_FALSE( source.open( outsidePath.view() ).isValid() ); // `<root>x` 는 다른 폴더다
    outsidePath.clear();
    outsidePath.appendFormat( "%#/../escape.pack", mount.str() );
    SW_EXPECT_FALSE( source.open( outsidePath.view() ).isValid() ); // `..` 성분은 거울 밖이다
    outsidePath.clear();
    outsidePath.appendFormat( "%#/sub/../a.pack", mount.str() );
    SW_EXPECT_FALSE( source.isFile( outsidePath.view() ) );
    SW_EXPECT_EQUAL( 0u, client._requestCount );

    vector<string> listPath;
    SW_REQUIRE_TRUE( source.collectFiles( mount.str(), listPath ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listPath.size() );
    StringBuilder<constant::buffer::kMax512> expectedPath;
    expectedPath.appendFormat( "%#/a.pack", FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( mount.str() ) ) );
    SW_EXPECT_TRUE( string_view( listPath[0] ) == expectedPath.view() );

    client._listExtraName.push_back( "../escape.pack" );
    listPath.clear();
    SW_EXPECT_LOG( "FileServe.BadListName" );
    SW_EXPECT_FALSE( source.collectFiles( mount.str(), listPath ) );
    SW_EXPECT_TRUE( listPath.empty() );
}

/**
 * @brief [FileServeContentSourceTest] 질의 값 인코딩 — 예약하지 않은 글자와 `/` 는 그대로, 나머지는 `%XX`. 결함이면 빈칸 · `&` 가 든 이름이 다른 질의 칸으로 읽힌다
 */
SW_TEST_CASE( FileServeContentSourceTest, EncodesQueryValues )
{
    SW_EXPECT_EQUAL( string( "maps/a-b_c.d~e" ), FileServeContentSource::encodeQueryValue( "maps/a-b_c.d~e" ) );
    SW_EXPECT_EQUAL( string( "a%20b%26c%25" ), FileServeContentSource::encodeQueryValue( "a b&c%" ) );
}
