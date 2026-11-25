#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

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
    const AbsolutePath                             packPath  = test::makeScratchPath( "SwPackWriterTest.pack" );
    const vector<std::pair<string, vector<uint8>>> listEntry = Util::makeEntries();
    SW_REQUIRE_TRUE( PackTestUtil::writePack( packPath, PackTestUtil::makeDesc( "game/test", PackCompressionKind::Zlib ), listEntry ) );

    PackReader reader;
    SW_REQUIRE_TRUE( reader.open( packPath.str() ) );
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
    const AbsolutePath packPath = test::makeScratchPath( "SwPackWriterOrder.pack" );
    string             first    = "a.txt";
    string             second   = "b.txt";
    if ( PackStreamWriter::makePathHash( first ) < PackStreamWriter::makePathHash( second ) )
        std::swap( first, second ); // 두 번째가 해시가 더 작게

    PackStreamWriter outOfOrder;
    SW_REQUIRE_TRUE( outOfOrder.open( packPath, PackTestUtil::makeDesc( "game/test" ) ).has_value() );
    SW_REQUIRE_TRUE( outOfOrder.append( first, PackEntryStorage::Blocks, vector<uint8>{ 1, 2, 3 } ).has_value() );
    const expected<void> refused = outOfOrder.append( second, PackEntryStorage::Blocks, vector<uint8>{ 4 } );
    SW_REQUIRE_FALSE( refused.has_value() );
    SW_EXPECT_TRUE( refused.error().kind() == ErrorKind::InvalidArgument );
    SW_EXPECT_FALSE( outOfOrder.append( "z.txt", PackEntryStorage::Blocks, vector<uint8>{} ).has_value() ); // 한 번 실패한 쓰기는 끝났다
    SW_EXPECT_FALSE( outOfOrder.close().has_value() );
    SW_EXPECT_FALSE( FileUtil::exists( packPath ) );

    PackStreamWriter twice;
    SW_REQUIRE_TRUE( twice.open( packPath, PackTestUtil::makeDesc( "game/test" ) ).has_value() );
    SW_REQUIRE_TRUE( twice.append( "Data/Same.xml", PackEntryStorage::Blocks, vector<uint8>{ 1 } ).has_value() );
    const expected<void> duplicate = twice.append( "data/same.xml", PackEntryStorage::Blocks, vector<uint8>{ 2 } );
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
    const AbsolutePath                       packPath  = test::makeScratchPath( "SwPackWriterHash.pack" );
    vector<std::pair<string, vector<uint8>>> listEntry = Util::makeEntries();
    VectorUtil::sort( listEntry, []( const std::pair<string, vector<uint8>>& left, const std::pair<string, vector<uint8>>& right )
    { return PackStreamWriter::makePathHash( left.first ) < PackStreamWriter::makePathHash( right.first ); } );
    PackStreamWriter writer;
    SW_REQUIRE_TRUE( writer.open( packPath, PackTestUtil::makeDesc( "game/test", PackCompressionKind::Zlib ) ).has_value() );
    for ( std::pair<string, vector<uint8>>& entry : listEntry )
    {
        SW_REQUIRE_TRUE( writer.append( entry.first, PackEntryStorage::Blocks, std::move( entry.second ) ).has_value() );
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

// 슬롯 배치는 앞 빌드의 자리를 지키고, 이번 빌드의 빈 자리가 한도를 넘으면 그 빌드에서 바로 촘촘히 다시 놓는다(한 빌드 늦게 걸러지지 않는다).
SW_TEST_CASE( PackWriterTest, SlotLayoutRepacksWhenThisBuildLeavesTooManyHoles )
{
    const AbsolutePath packPath   = test::makeScratchPath( "SwPackWriterSlot.pack" );
    const string       keptPath   = "data/kept.xml";
    const auto         writeSlots = [&packPath]( const vector<string>& listPath, uint8 fill ) -> optional<PackWriteResult>
    {
        vector<std::pair<string, vector<uint8>>> listEntry;
        for ( const string& path : listPath )
        {
            listEntry.emplace_back( path, vector<uint8>( 3 * kPackSectorAlignment, fill ) );
        }
        VectorUtil::sort( listEntry, []( const std::pair<string, vector<uint8>>& left, const std::pair<string, vector<uint8>>& right )
        { return PackStreamWriter::makePathHash( left.first ) < PackStreamWriter::makePathHash( right.first ); } );
        PackWriterDesc desc    = PackTestUtil::makeDesc( "game/test" );
        desc._layoutMode       = PackLayoutMode::Slot;
        desc._previousPackPath = packPath;
        desc._maxHolePercent   = 25;
        PackStreamWriter writer;
        expected<void>   step = writer.open( packPath, desc );
        for ( size_t index = 0; index < listEntry.size() && step.has_value(); ++index )
        {
            step = writer.append( listEntry[index].first, PackEntryStorage::Blocks, std::move( listEntry[index].second ) );
        }
        if ( step.has_value() == false )
            return nullopt;
        expected<PackWriteResult> closed = writer.close();
        return closed.has_value() ? optional<PackWriteResult>{ std::move( *closed ) } : nullopt;
    };
    const auto readOffset = [&packPath]( const string& path, uint64& outOffset )
    {
        PackReader    reader;
        PackFileEntry entry;
        const bool    bFound = reader.open( packPath.str() ) && reader.getFileEntry( path, entry );
        outOffset            = entry._dataOffset;
        return bFound;
    };

    SW_REQUIRE_TRUE( writeSlots( { keptPath, "data/a.xml", "data/b.xml", "data/c.xml" }, 1 ).has_value() );
    uint64 firstOffset = 0;
    SW_REQUIRE_TRUE( readOffset( keptPath, firstOffset ) );

    // 하나만 지우면 빈 자리가 한도(25 %) 안이다 — 남은 항목은 앞 빌드의 자리를 지킨다. 덮어쓴 팩의 내용 해시도 파일과 같다.
    const optional<PackWriteResult> kept = writeSlots( { keptPath, "data/a.xml", "data/b.xml" }, 2 );
    SW_REQUIRE_TRUE( kept.has_value() );
    uint64 keptOffset = 0;
    SW_REQUIRE_TRUE( readOffset( keptPath, keptOffset ) );
    SW_EXPECT_EQUAL( firstOffset, keptOffset );
    vector<uint8> fileBytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( packPath, fileBytes ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( fileBytes ), kept->_contentHash );

    // 셋을 지우면 이번 빌드의 빈 자리가 한도를 넘는다 — 그 자리에서 촘촘히 다시 놓아 남은 항목이 맨 앞(0)에 온다.
    const optional<PackWriteResult> dense = writeSlots( { keptPath }, 3 );
    SW_REQUIRE_TRUE( dense.has_value() );
    uint64 denseOffset = 1;
    SW_REQUIRE_TRUE( readOffset( keptPath, denseOffset ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, denseOffset );
    SW_REQUIRE_TRUE( FileUtil::readFile( packPath, fileBytes ) );
    SW_EXPECT_EQUAL( ContentHash::computeBytes( fileBytes ), dense->_contentHash );
    PackReader reader;
    SW_REQUIRE_TRUE( reader.open( packPath.str() ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( reader.readFile( keptPath, bytes ) );
    SW_EXPECT_TRUE( bytes == vector<uint8>( 3 * kPackSectorAlignment, uint8{ 3 } ) );
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}
