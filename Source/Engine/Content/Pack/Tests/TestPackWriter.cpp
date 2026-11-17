#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/ContentHash.h"
#include "Engine/Content/Pack/PackReader.h"
#include "Engine/Content/Pack/PackWriter.h"
#include "Engine/Content/Pack/Tests/PackTestUtil.h"

// 팩 스트림 쓰기(형식 3) — 경로 해시 순으로 받아 블록마다 압축해 이어 쓰고, 끝에 색인 · 헤더를 쓴다. 쓰는 동안 센 내용 해시가 파일의 해시와 같다.

using namespace sw;

namespace
{
    struct PackWriterTestUtil
    {
        /** @brief 결정적인 바이트 @p size 개입니다. 앞 절반은 흩어지고 뒤 절반은 같은 바이트라 블록마다 압축률이 다르다. */
        static vector<uint8> makeBytes( uint32 seed, size_t size )
        {
            vector<uint8> bytes( size );
            uint32        state = seed * 2654435761u + 1u;
            for ( size_t index = 0; index < size; ++index )
            {
                state        = state * 1664525u + 1013904223u;
                bytes[index] = index < size / 2 ? static_cast<uint8>( state >> 24 ) : static_cast<uint8>( seed );
            }
            return bytes;
        }

        /** @brief 빈 항목 · 작은 항목 · 블록 여럿을 걸치는 항목, 합이 쓰기 창을 여러 번 넘는 목록입니다. */
        static vector<std::pair<string, vector<uint8>>> makeEntries()
        {
            vector<std::pair<string, vector<uint8>>> listEntry;
            listEntry.emplace_back( "empty.txt", vector<uint8>{} );
            listEntry.emplace_back( "maps/town.xml", makeBytes( 1, 7 ) );
            listEntry.emplace_back( "maps/deep/cave.xml", makeBytes( 2, PackTestUtil::kBlockSize ) );         // 블록 경계에 꼭 맞는다
            listEntry.emplace_back( "textures/big.dds", makeBytes( 3, 3 * PackTestUtil::kBlockSize + 123 ) ); // 블록 넷, 마지막이 짧다
            listEntry.emplace_back( "textures/huge.dds", makeBytes( 4, 2 * PackTestUtil::kWriteWindowBytes ) );
            for ( uint32 index = 0; index < 40; ++index )
            {
                string path = "data/item";
                path.push_back( static_cast<utf8>( '0' + index / 10 ) );
                path.push_back( static_cast<utf8>( '0' + index % 10 ) );
                path += ".xml";
                listEntry.emplace_back( path, makeBytes( 10 + index, 1000 + index * 97 ) );
            }
            return listEntry;
        }
    };
    using Util = PackWriterTestUtil;
} // namespace

SW_TEST_CASE( PackWriterTest, StreamedPackReadsBackEveryEntry )
{
    const string                                   packPath  = test::makeScratchPath( "SwPackWriterTest.pack" );
    const vector<std::pair<string, vector<uint8>>> listEntry = Util::makeEntries();
    SW_REQUIRE_TRUE( PackTestUtil::writePack( packPath, PackTestUtil::makeDesc( "game/test", PackCompressionType::Zlib ), listEntry ) );

    PackReader reader;
    SW_REQUIRE_TRUE( reader.open( packPath ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( listEntry.size() ), reader.getFileCount() );
    SW_EXPECT_TRUE( reader.getMountDomain() == "game/test" );
    SW_EXPECT_EQUAL( PackTestUtil::kBlockSize, static_cast<uint32>( reader.getHeader()._blockSize ) ); // 묶음 칸은 참조로 넘기지 않는다
    for ( const std::pair<string, vector<uint8>>& entry : listEntry )
    {
        vector<uint8> bytes;
        SW_REQUIRE_TRUE( reader.readFile( entry.first, bytes ) );
        SW_EXPECT_TRUE( bytes == entry.second );
    }
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}

// 순서가 틀리거나 같은 경로(대소문자 무시 해시)를 두 번 담으면 오류이고, 그 쓰기는 끝난다 — 대상 파일은 생기지 않는다.
SW_TEST_CASE( PackWriterTest, OutOfOrderAppendIsAnError )
{
    const string packPath = test::makeScratchPath( "SwPackWriterOrder.pack" );
    string       first    = "a.txt";
    string       second   = "b.txt";
    if ( PackStreamWriter::makePathHash( first ) < PackStreamWriter::makePathHash( second ) )
        std::swap( first, second ); // 두 번째가 해시가 더 작게

    PackStreamWriter outOfOrder;
    SW_REQUIRE_TRUE( outOfOrder.open( packPath, PackTestUtil::makeDesc( "game/test" ) ).has_value() );
    SW_REQUIRE_TRUE( outOfOrder.append( first, vector<uint8>{ 1, 2, 3 } ).has_value() );
    const expected<void> refused = outOfOrder.append( second, vector<uint8>{ 4 } );
    SW_REQUIRE_FALSE( refused.has_value() );
    SW_EXPECT_TRUE( refused.error().kind() == ErrorKind::InvalidArgument );
    SW_EXPECT_FALSE( outOfOrder.append( "z.txt", vector<uint8>{} ).has_value() ); // 한 번 실패한 쓰기는 끝났다
    SW_EXPECT_FALSE( outOfOrder.close().has_value() );
    SW_EXPECT_FALSE( FileUtil::exists( packPath ) );

    PackStreamWriter twice;
    SW_REQUIRE_TRUE( twice.open( packPath, PackTestUtil::makeDesc( "game/test" ) ).has_value() );
    SW_REQUIRE_TRUE( twice.append( "Data/Same.xml", vector<uint8>{ 1 } ).has_value() );
    const expected<void> duplicate = twice.append( "data/same.xml", vector<uint8>{ 2 } );
    SW_REQUIRE_FALSE( duplicate.has_value() );
    SW_EXPECT_TRUE( duplicate.error().kind() == ErrorKind::AlreadyExists );
    SW_EXPECT_FALSE( twice.close().has_value() );
    SW_EXPECT_FALSE( FileUtil::exists( packPath ) );

    // 블록 크기 · 창은 기본값이 없다 — 비워 두면 열지 않는다.
    PackWriterDesc   noBlockSize = PackTestUtil::makeDesc( "game/test" );
    PackStreamWriter unset;
    noBlockSize._blockSize = 0;
    SW_EXPECT_FALSE( unset.open( packPath, noBlockSize ).has_value() );
}

// 쓰는 동안 센 내용 해시가 쓴 파일을 읽어 센 해시와 같다 — 쿠킹 기록이 출력을 다시 읽지 않는 근거다.
SW_TEST_CASE( PackWriterTest, WriteResultHashEqualsHashOfTheFile )
{
    const string                             packPath  = test::makeScratchPath( "SwPackWriterHash.pack" );
    vector<std::pair<string, vector<uint8>>> listEntry = Util::makeEntries();
    VectorUtil::sort( listEntry, []( const std::pair<string, vector<uint8>>& left, const std::pair<string, vector<uint8>>& right )
    { return PackStreamWriter::makePathHash( left.first ) < PackStreamWriter::makePathHash( right.first ); } );
    PackStreamWriter writer;
    SW_REQUIRE_TRUE( writer.open( packPath, PackTestUtil::makeDesc( "game/test", PackCompressionType::Zlib ) ).has_value() );
    for ( std::pair<string, vector<uint8>>& entry : listEntry )
    {
        SW_REQUIRE_TRUE( writer.append( entry.first, std::move( entry.second ) ).has_value() );
    }
    const expected<PackWriteResult> result = writer.close();
    SW_REQUIRE_TRUE( result.has_value() );

    vector<uint8> fileBytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( packPath, fileBytes ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( fileBytes ), result->_contentHash );
    SW_EXPECT_EQUAL( static_cast<uint64>( fileBytes.size() ), result->_fileSize );
    SW_EXPECT_TRUE( result->_indexOffset < result->_fileSize );
    SW_EXPECT_EQUAL( static_cast<uint32>( listEntry.size() ), result->_stats._entryCount );
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}
