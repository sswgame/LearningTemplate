#include "pch.h"

#include "Core/File/AsyncFileIO.h"
#include "Core/File/ContentFileSource.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    struct ContentFileSourceTestUtil
    {
        static vector<uint8> toBytes( string_view text ) { return vector<uint8>( text.begin(), text.end() ); }

        static MemoryContentFileSource makeSource()
        {
            MemoryContentFileSource source;
            source.addFile( "/packs/a.pack", toBytes( "0123456789" ) );
            source.addFile( "/packs/sub/b.pack", toBytes( "nested" ) );
            return source;
        }
    };
    using Util = ContentFileSourceTestUtil;
} // namespace

/** @brief [ContentFileSourceTest] 메모리 소스는 있음 · 폴더 · 바로 아래 나열을 디스크와 같은 뜻으로 답합니다. */
SW_TEST_CASE( ContentFileSourceTest, MemorySourceAnswersPresenceAndListing )
{
    const MemoryContentFileSource source = Util::makeSource();
    SW_EXPECT_TRUE( source.isFile( "/packs/a.pack" ) );
    SW_EXPECT_FALSE( source.isFile( "/packs" ) );
    SW_EXPECT_TRUE( source.isFolder( "/packs" ) );
    SW_EXPECT_TRUE( source.isFolder( "/packs/" ) );
    SW_EXPECT_FALSE( source.isFolder( "/pac" ) );

    vector<string> listPath;
    SW_REQUIRE_TRUE( source.collectFiles( "/packs", listPath ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listPath.size() ); // 하위 폴더 안은 빠진다
    SW_EXPECT_TRUE( listPath[0] == "/packs/a.pack" );
}

/** @brief [ContentFileSourceTest] 메모리 핸들의 위치 지정 읽기는 끝을 넘으면 짧게 읽지 않고 실패합니다. */
SW_TEST_CASE( ContentFileSourceTest, MemoryHandleReadsExactRanges )
{
    const MemoryContentFileSource source = Util::makeSource();
    SW_EXPECT_FALSE( source.open( "/missing" ).isValid() );

    const AsyncFileHandle file = source.open( "/packs/a.pack" );
    SW_REQUIRE_TRUE( file.isValid() );
    SW_EXPECT_EQUAL( uint64{ 10 }, file.getSize() );

    utf8 arrByte[3]{};
    SW_REQUIRE_TRUE( file.readAt( 7, arrByte, 3 ) );
    SW_EXPECT_TRUE( arrByte[0] == '7' && arrByte[2] == '9' );
    SW_EXPECT_FALSE( file.readAt( 8, arrByte, 3 ) );
    SW_EXPECT_FALSE( file.readAt( 11, arrByte, 1 ) );
}

/** @brief [ContentFileSourceTest] 메모리 파일도 비동기 읽기가 같은 큐 · 같은 결과로 끝납니다(구간 · 끝을 넘는 구간). */
SW_TEST_CASE( ContentFileSourceTest, MemoryFileGoesThroughAsyncFileIO )
{
    for ( const AsyncIOBackendKind kind : { AsyncIOBackendKind::Auto, AsyncIOBackendKind::ThreadPool } )
    {
        AsyncFileIO       io;
        AsyncFileIOConfig settings;
        settings._backendKind = kind;
        SW_REQUIRE_TRUE( io.initialize( settings ) );

        const MemoryContentFileSource source = Util::makeSource();
        const AsyncFileHandle         file   = source.open( "/packs/a.pack" );

        vector<uint8>   bytes;
        AsyncReadHandle range = io.readRange( file, 2, 3, AsyncIOPriority::Normal, SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [&bytes]( AsyncReadResult& result )
        { bytes = std::move( result._bytes ); } ) );
        range.wait();
        SW_EXPECT_TRUE( range.getStatus() == AsyncIOStatus::Succeeded );
        SW_EXPECT_TRUE( bytes == Util::toBytes( "234" ) );

        AsyncReadHandle tooFar = io.readRange( file, 8, 3, AsyncIOPriority::Normal );
        tooFar.wait();
        SW_EXPECT_TRUE( tooFar.getStatus() == AsyncIOStatus::OutOfRange );

        AsyncReadHandle whole = source.readFileAsync( io, "/packs/sub/b.pack", AsyncIOPriority::High, {} );
        whole.wait();
        SW_EXPECT_TRUE( whole.getStatus() == AsyncIOStatus::Succeeded );

        io.shutdown();
    }
}

/** @brief [ContentFileSourceTest] 건 소스가 지금 소스이고, 놓으면 플랫폼 기본으로 돌아갑니다. */
SW_TEST_CASE( ContentFileSourceTest, BindReplacesTheCurrentSource )
{
    const IContentFileSource&     platformSource = ContentFileSource::getCurrent();
    const MemoryContentFileSource source         = Util::makeSource();
    const IContentFileSource*     pPrevious      = ContentFileSource::bindCurrent( &source );
    SW_EXPECT_TRUE( ContentFileSource::getCurrent().isFile( "/packs/a.pack" ) );
    (void)ContentFileSource::bindCurrent( pPrevious );
    SW_EXPECT_TRUE( &ContentFileSource::getCurrent() == &platformSource );
}
