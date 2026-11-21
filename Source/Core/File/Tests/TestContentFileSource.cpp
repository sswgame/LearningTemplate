#include "pch.h"

#include "Core/File/AsyncFileIO.h"
#include "Core/File/ContentFileSource.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    struct ContentFileSourceTestUtil
    {
        static vector<uint8> toBytes( string_view text ) { return vector<uint8>( text.begin(), text.end() ); }

        /** @brief @p source 의 @p path 를 통째로 읽은 것과 구간 [@p offset, @p offset + @p size) 를 읽은 것이 같은 바이트인지 봅니다. */
        static void expectRangeMatchesWholeRead( AsyncFileIO& io, const IContentFileSource& source, string_view path, uint64 offset, uint64 size )
        {
            vector<uint8>   whole;
            AsyncReadHandle wholeRead = source.readFileAsync( io, path, AsyncIOPriority::Normal, SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [&whole]( AsyncReadResult& result )
            { whole = std::move( result._bytes ); } ) );
            wholeRead.wait();
            SW_REQUIRE_TRUE( wholeRead.getStatus() == AsyncIOStatus::Succeeded );

            vector<uint8>   range;
            AsyncReadHandle rangeRead = source.readRangeAsync( io, path, offset, size, AsyncIOPriority::Normal, SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [&range]( AsyncReadResult& result )
            { range = std::move( result._bytes ); } ) );
            rangeRead.wait();
            SW_REQUIRE_TRUE( rangeRead.getStatus() == AsyncIOStatus::Succeeded );
            SW_REQUIRE_EQUAL( size_t( size ), range.size() );
            SW_EXPECT_TRUE( vector<uint8>( whole.begin() + static_cast<ptrdiff_t>( offset ), whole.begin() + static_cast<ptrdiff_t>( offset + size ) ) == range );

            AsyncReadHandle tooFar = source.readRangeAsync( io, path, whole.size() - 1, 2, AsyncIOPriority::Normal, {} );
            tooFar.wait();
            SW_EXPECT_TRUE( tooFar.getStatus() == AsyncIOStatus::OutOfRange ); // 끝을 넘으면 짧게 읽지 않는다
        }

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

/** @brief [ContentFileSourceTest] 메모리 · 디스크 소스의 구간 읽기가 통째 읽기의 같은 자리와 바이트까지 같고, 끝을 넘으면 `OutOfRange` 다(밉 범위 읽기 — MR-1). */
SW_TEST_CASE( ContentFileSourceTest, RangeMatchesWholeRead )
{
    AsyncFileIO io;
    SW_REQUIRE_TRUE( io.initialize( AsyncFileIOConfig{} ) );

    const MemoryContentFileSource memory = Util::makeSource();
    Util::expectRangeMatchesWholeRead( io, memory, "/packs/a.pack", 3, 5 );

    const AbsolutePath  diskPath = test::makeScratchPath( "ContentFileSourceTest.range.bin" );
    const vector<uint8> bytes    = Util::toBytes( "header|mip0mip0mip0|mip1|m2" );
    SW_REQUIRE_TRUE( FileUtil::writeFile( diskPath, bytes.data(), bytes.size() ) );
    const DiskContentFileSource disk;
    Util::expectRangeMatchesWholeRead( io, disk, diskPath.str(), 7, 12 );

    io.shutdown();
}
