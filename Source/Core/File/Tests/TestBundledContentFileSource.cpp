/**
 * @file TestBundledContentFileSource.cpp
 * @brief 앱 묶음 소스(`BundledContentFileSource`) — 경로 ↔ 묶음 이름 옮기기, 나열, 묶음 파일 안 구간 읽기(`AsyncFileHandle::openNativeRange`).
 * @details 가짜 묶음 읽기가 APK 처럼 한 파일 안에 여러 파일을 압축 없이 이어 둔 묶음 파일을 만들고, 이름마다 (시작, 길이) 구간을 연다 — Android 의 `AAsset_openFileDescriptor64` 와 같은 모양이다.
 *          그래서 데스크톱에서 Android 의 팩 읽기 길(경로 옮기기 → 구간 핸들 → 위치 지정 · 비동기 읽기)을 그대로 지난다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/BundledContentFileSource.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 묶음 파일 하나에 파일들을 이어 둔 가짜 묶음 읽기입니다. 앞 · 사이에 다른 바이트를 끼워 구간 시작이 0 이 아니게 한다. */
    class FakeBundleReader final : public IBundledFileReader
    {
    public:
        struct Entry
        {
            string _name;
            uint64 _offset;
            uint64 _size;
        };

        explicit FakeBundleReader( const utf8* pScratchName )
            : _bundlePath{ test::makeScratchPath( pScratchName ) }
            , _listEntry{}
        {
        }

        /** @brief @p listFile 을 (이름, 내용) 순서로 이어 묶음 파일을 씁니다. */
        bool write( const vector<std::pair<string, string>>& listFile )
        {
            vector<uint8> bytes{ 'A', 'P', 'K', '!' }; // 묶음 머리 — 첫 파일도 0 에서 시작하지 않는다
            for ( const auto& [name, content] : listFile )
            {
                _listEntry.push_back( Entry{ name, bytes.size(), content.size() } );
                bytes.insert( bytes.end(), content.begin(), content.end() );
                bytes.push_back( '#' ); // 다음 파일 앞 구분 — 구간 끝을 넘어 읽으면 섞인다
            }
            return FileUtil::writeFile( _bundlePath, bytes.data(), bytes.size() );
        }

        AsyncFileHandle open( string_view bundledName ) const override
        {
            for ( const Entry& entry : _listEntry )
            {
                if ( entry._name == bundledName )
                    return AsyncFileHandle::openNativeRange( PlatformFileUtil::openNativeFileForRead( _bundlePath.c_str() ), entry._offset, entry._size, entry._name );
            }
            return AsyncFileHandle{};
        }

        bool collectFileNames( string_view bundledFolder, vector<string>& outListFileName ) const override
        {
            const string prefix = string( bundledFolder ) + "/";
            for ( const Entry& entry : _listEntry )
            {
                const bool bUnder = entry._name.compare( 0, prefix.size(), prefix ) == 0 && entry._name.find( '/', prefix.size() ) == string::npos;
                if ( bUnder )
                    outListFileName.push_back( entry._name.substr( prefix.size() ) );
            }
            return true;
        }

    private:
        string        _bundlePath;
        vector<Entry> _listEntry;
    };

    vector<uint8> toBytes( string_view text )
    {
        return vector<uint8>( text.begin(), text.end() );
    }
} // namespace

/** @brief [BundledContentFileSourceTest] 걸린 폴더와 그 아래만 묶음 이름으로 옮기고, 이름이 같은 앞머리의 다른 폴더 · 밖 경로는 옮기지 않습니다. */
SW_TEST_CASE( BundledContentFileSourceTest, MapsOnlyPathsUnderTheMountRoot )
{
    string name;
    SW_REQUIRE_TRUE( BundledContentFileSource::tryMapToBundledName( "/data/app/files/Packs/engine.pack", "/data/app/files/Packs", "Packs", name ) );
    SW_EXPECT_TRUE( name == "Packs/engine.pack" );
    SW_REQUIRE_TRUE( BundledContentFileSource::tryMapToBundledName( "/data/app/files/Packs/", "/data/app/files/Packs", "Packs", name ) );
    SW_EXPECT_TRUE( name == "Packs" );
    SW_REQUIRE_TRUE( BundledContentFileSource::tryMapToBundledName( "C:\\Game\\Packs\\sub\\a.pack", "C:/Game/Packs/", "Packs", name ) );
    SW_EXPECT_TRUE( name == "Packs/sub/a.pack" );
    SW_EXPECT_FALSE( BundledContentFileSource::tryMapToBundledName( "/data/app/files/PacksOld/a.pack", "/data/app/files/Packs", "Packs", name ) );
    SW_EXPECT_FALSE( BundledContentFileSource::tryMapToBundledName( "/data/app/files/Saved/log.txt", "/data/app/files/Packs", "Packs", name ) );
    SW_EXPECT_FALSE( BundledContentFileSource::tryMapToBundledName( "/data/app/files/packs/a.pack", "/data/app/files/Packs", "Packs", name ) ); // 묶음 이름은 대소문자를 가린다
}

/** @brief [BundledContentFileSourceTest] 나열은 걸린 경로 아래 경로로 돌려주고, 묶음 안 파일은 구간만 읽힙니다(앞뒤 파일이 섞이지 않는다). */
SW_TEST_CASE( BundledContentFileSourceTest, ListsAndReadsFilesInsideTheBundle )
{
    FakeBundleReader reader( "BundledContentFileSourceTest.bundle" );
    SW_REQUIRE_TRUE( reader.write( {
        {  "Packs/engine.pack",       "0123456789"},
        {    "Packs/game.pack", "\xEF\xBB\xBFgame"},
        {"Packs/sub/skip.pack",                "x"},
        {        "Other/a.txt",                "y"}
    } ) );
    const BundledContentFileSource source( reader, "/app/Packs", "Packs" );

    SW_EXPECT_TRUE( source.isFolder( "/app/Packs" ) );
    SW_EXPECT_TRUE( source.isFolder( "/app/Packs/sub" ) );
    SW_EXPECT_FALSE( source.isFolder( "/app/Packs/none" ) );
    SW_EXPECT_FALSE( source.isFolder( "/app/Saved" ) );
    SW_EXPECT_TRUE( source.isFile( "/app/Packs/engine.pack" ) );
    SW_EXPECT_FALSE( source.isFile( "/app/Packs" ) );
    SW_EXPECT_FALSE( source.isFile( "/app/Other/a.txt" ) ); // 걸린 폴더 밖은 묶음에 있어도 보이지 않는다

    vector<string> listPath;
    SW_REQUIRE_TRUE( source.collectFiles( "/app/Packs/", listPath ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPath.size() ); // 하위 폴더 안은 빠진다
    SW_EXPECT_TRUE( listPath[0] == "/app/Packs/engine.pack" );
    SW_EXPECT_TRUE( listPath[1] == "/app/Packs/game.pack" );

    const AsyncFileHandle file = source.open( "/app/Packs/engine.pack" );
    SW_REQUIRE_TRUE( file.isValid() );
    SW_EXPECT_EQUAL( uint64{ 10 }, file.getSize() );
    utf8 arrByte[3]{};
    SW_REQUIRE_TRUE( file.readAt( 7, arrByte, 3 ) );
    SW_EXPECT_TRUE( arrByte[0] == '7' && arrByte[2] == '9' );
    SW_EXPECT_FALSE( file.readAt( 8, arrByte, 3 ) ); // 구간 끝을 넘으면 묶음 파일에 바이트가 있어도 실패한다

    vector<uint8> bytes;
    SW_REQUIRE_TRUE( source.readFile( "/app/Packs/engine.pack", bytes ) );
    SW_EXPECT_TRUE( bytes == toBytes( "0123456789" ) );
    string text;
    SW_REQUIRE_TRUE( source.readTextFile( "/app/Packs/game.pack", text ) );
    SW_EXPECT_TRUE( text == "game" ); // BOM 을 뗀다
}

/** @brief [BundledContentFileSourceTest] 묶음 안 구간 파일도 비동기 읽기가 파일 기준 위치 · 구간 끝으로 끝납니다(두 백엔드). */
SW_TEST_CASE( BundledContentFileSourceTest, BundledRangeGoesThroughAsyncFileIO )
{
    FakeBundleReader reader( "BundledContentFileSourceTest.async.bundle" );
    SW_REQUIRE_TRUE( reader.write( {
        { "Packs/first.pack",        "abc"},
        {"Packs/second.pack", "0123456789"}
    } ) );
    const BundledContentFileSource source( reader, "/app/Packs", "Packs" );
    for ( const AsyncIOBackendKind kind : { AsyncIOBackendKind::Auto, AsyncIOBackendKind::ThreadPool } )
    {
        AsyncFileIO       io;
        AsyncFileIOConfig settings;
        settings._backendKind = kind;
        SW_REQUIRE_TRUE( io.initialize( settings ) );

        vector<uint8>   bytes;
        uint64          resultOffset{ 0 };
        AsyncReadHandle range = io.readRange( source.open( "/app/Packs/second.pack" ), 2, 3, AsyncIOPriority::Normal,
                                              SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [&bytes, &resultOffset]( AsyncReadResult& result )
        {
            bytes        = std::move( result._bytes );
            resultOffset = result._offset;
        } ) );
        range.wait();
        SW_EXPECT_TRUE( range.getStatus() == AsyncIOStatus::Succeeded );
        SW_EXPECT_TRUE( bytes == toBytes( "234" ) );
        SW_EXPECT_EQUAL( uint64{ 2 }, resultOffset ); // 묶음 파일 안 위치가 아니라 파일 기준 위치다

        AsyncReadHandle tooFar = io.readRange( source.open( "/app/Packs/second.pack" ), 8, 3, AsyncIOPriority::Normal );
        tooFar.wait();
        SW_EXPECT_TRUE( tooFar.getStatus() == AsyncIOStatus::OutOfRange );

        vector<uint8>   whole;
        AsyncReadHandle wholeRead = source.readFileAsync( io, "/app/Packs/first.pack", AsyncIOPriority::High,
                                                          SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [&whole]( AsyncReadResult& result )
        { whole = std::move( result._bytes ); } ) );
        wholeRead.wait();
        SW_EXPECT_TRUE( wholeRead.getStatus() == AsyncIOStatus::Succeeded );
        SW_EXPECT_TRUE( whole == toBytes( "abc" ) );

        io.shutdown();
    }
}
