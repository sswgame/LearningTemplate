#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/Tests/FakeNetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/PackReader.h"
#include "Engine/Content/Pack/Tests/PackTestUtil.h"

// 색인 서명(PK-5) — 패치 목록은 팩 전체가 아니라 색인 구역의 요약에 서명하고, 리더는 여는 핸들로 색인을 견준 뒤에만 열며 읽을 때 블록마다 견준다.
// SHA-256 은 가짜 제공자(`FakeNetSecurityProvider::computeFakeDigest`)로 본다 — 규칙(어느 바이트를 요약하고 언제 견주는가)이 대상이다.

using namespace sw;

namespace
{
    struct PackPatchTestUtil
    {
        /** @brief 서명 채널의 패치 팩(블록 해시 표 + 색인 요약)을 씁니다. */
        static optional<PackWriteResult> writeSignedPack( const AbsolutePath& packPath, INetSecurityProvider& provider )
        {
            vector<std::pair<string, vector<uint8>>> listEntry;
            listEntry.emplace_back( "game/test/data/a.xml", vector<uint8>( 3000, uint8{ 'a' } ) );
            listEntry.emplace_back( "game/test/data/b.xml", vector<uint8>( 2 * PackTestUtil::kBlockSize + 9, uint8{ 'b' } ) );
            VectorUtil::sort( listEntry, []( const std::pair<string, vector<uint8>>& left, const std::pair<string, vector<uint8>>& right )
            { return PackStreamWriter::makePathHash( left.first ) < PackStreamWriter::makePathHash( right.first ); } );
            PackWriterDesc desc   = PackTestUtil::makeDesc( "" );
            desc._pDigestProvider = &provider;
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
        }

        /** @brief 파일의 @p offset 바이트를 뒤집습니다. */
        static bool writeFlippedByte( const AbsolutePath& path, uint64 offset )
        {
            vector<uint8> bytes;
            if ( FileUtil::readFile( path, bytes ) == false || offset >= bytes.size() )
                return false;
            bytes[static_cast<size_t>( offset )] ^= 0x5Au;
            return FileUtil::writeFile( path, bytes.data(), bytes.size() );
        }

        /** @brief 파일 끝의 헤더를 읽습니다. */
        static PackHeader readHeader( const AbsolutePath& path )
        {
            vector<uint8> bytes;
            PackHeader    header{};
            if ( FileUtil::readFile( path, bytes ) && bytes.size() >= sizeof( PackHeader ) )
                Memory::copy( &header, bytes.data() + bytes.size() - sizeof( PackHeader ), sizeof( PackHeader ) );
            return header;
        }
    };
    using Util = PackPatchTestUtil;
} // namespace

// 서명 뒤 색인 구역이 바뀐 팩은 마운트하지 않는다. 블록 해시 표가 없는 팩도 신뢰를 받아 마운트하지 않는다.
SW_TEST_CASE( PackPatchTest, TamperedIndexFailsAtMount )
{
    SW_EXPECT_LOG( "PackReader.IndexDigestMismatch" );
    SW_EXPECT_LOG( "PackReader.TrustedPackWithoutBlockHashes" );
    SW_EXPECT_LOG( "PackReader.FailedLoadFATIndex" );
    SW_EXPECT_LOG( "PackManager.FailedOpenMountPack" );
    test::FakeNetSecurityProvider provider;
    provider._bHashOk           = SW_TRUE;
    const AbsolutePath packPath = test::makeScratchPath( "patch_2.pack" );
    const auto         written  = Util::writeSignedPack( packPath, provider );
    SW_REQUIRE_TRUE( written.has_value() && written->_indexDigest.has_value() );
    const PackIndexTrust trust{ &provider, *written->_indexDigest };

    PackManager packs;
    SW_REQUIRE_TRUE( packs.mountPatchPack( packPath.str(), 100, {}, &trust ) );
    SW_EXPECT_TRUE( packs.unmountPack( packPath.str() ) );

    const PackHeader header = Util::readHeader( packPath );
    SW_REQUIRE_TRUE( Util::writeFlippedByte( packPath, header._stringPoolOffset + 1 ) ); // 첫 항목 경로의 첫 글자
    SW_EXPECT_FALSE( packs.mountPatchPack( packPath.str(), 100, {}, &trust ) );

    // 서명하지 않은 쓰기의 팩(블록 해시 표 없음)은 색인 요약이 맞아도 신뢰로 열지 않는다.
    SW_REQUIRE_TRUE( PackTestUtil::writeTextPack( packPath, "", {
                                                                    { "game/test/data/a.xml", "a" }
    } ) );
    vector<uint8> fileBytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( packPath, fileBytes ) );
    const PackHeader unsignedHeader = Util::readHeader( packPath );
    PackIndexTrust   unsignedTrust{ &provider, {} };
    test::FakeNetSecurityProvider::computeFakeDigest( fileBytes.data() + unsignedHeader._indexOffset, static_cast<int32>( fileBytes.size() - unsignedHeader._indexOffset ),
                                                      unsignedTrust._indexDigest._arrByte );
    SW_EXPECT_FALSE( packs.mountPatchPack( packPath.str(), 100, {}, &unsignedTrust ) );
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}

// 색인은 그대로이고 페이로드 블록 하나가 바뀐 팩은 열리지만, 그 블록을 읽는 순간 거절한다 — 다른 항목은 읽힌다.
SW_TEST_CASE( PackPatchTest, TamperedPayloadBlockFailsAtRead )
{
    SW_EXPECT_LOG( "PackReader.BlockDigestMismatch" );
    test::FakeNetSecurityProvider provider;
    provider._bHashOk           = SW_TRUE;
    const AbsolutePath packPath = test::makeScratchPath( "patch_3.pack" );
    const auto         written  = Util::writeSignedPack( packPath, provider );
    SW_REQUIRE_TRUE( written.has_value() && written->_indexDigest.has_value() );
    const PackIndexTrust trust{ &provider, *written->_indexDigest };

    // 페이로드는 오프셋 0 부터다 — 그 자리의 항목(경로 해시 순 첫 항목)의 첫 블록을 바꾼다.
    SW_REQUIRE_TRUE( Util::writeFlippedByte( packPath, 0 ) );
    PackReader reader;
    SW_REQUIRE_TRUE( reader.open( packPath.str(), &trust ) );
    string        tamperedPath;
    string        intactPath;
    PackFileEntry entry;
    for ( const string& path : { string( "game/test/data/a.xml" ), string( "game/test/data/b.xml" ) } )
    {
        SW_REQUIRE_TRUE( reader.getFileEntry( path, entry ) );
        ( entry._dataOffset == 0 ? tamperedPath : intactPath ) = path;
    }
    vector<uint8> bytes;
    SW_EXPECT_FALSE( reader.readFile( tamperedPath, bytes ) );
    SW_EXPECT_TRUE( reader.readFile( intactPath, bytes ) );
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}

/** @brief [PackPatchTest] 암호 제공자(모듈의 것)가 내려간 뒤에는 서명한 팩을 요약 없이 읽지 않고 오류로 진다 — 사라진 제공자를 부르지 않는다 */
SW_TEST_CASE( PackPatchTest, RetiredProviderRefusesSignedReads )
{
    SW_EXPECT_LOG( "PackReader.DigestProviderRetired" );
    const AbsolutePath packPath = test::makeScratchPath( "patch_4.pack" );
    PackManager        packs;
    vector<uint8>      bytes;
    {
        test::FakeNetSecurityProvider provider;
        provider._bHashOk  = SW_TRUE;
        const auto written = Util::writeSignedPack( packPath, provider );
        SW_REQUIRE_TRUE( written.has_value() && written->_indexDigest.has_value() );
        const PackIndexTrust trust{ &provider, *written->_indexDigest };
        SW_REQUIRE_TRUE( packs.mountPatchPack( packPath.str(), 100, {}, &trust ) );
        SW_EXPECT_TRUE( packs.readFile( "game/test/data/a.xml", bytes ) );
        provider.retire();
    }
    SW_EXPECT_FALSE( packs.readFile( "game/test/data/a.xml", bytes ) );
    packs.unmountAll();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}

// 서명 확인 비용은 색인 크기에만 비례하고 팩 크기 한도가 없다 — 페이로드가 2 GiB 너머에 있는 팩(앞은 성긴 파일의 구멍)을 신뢰로 열고 읽는다.
SW_TEST_CASE( PackPatchTest, PatchOverTwoGiBIsAccepted )
{
    test::FakeNetSecurityProvider provider;
    provider._bHashOk                = SW_TRUE;
    const AbsolutePath packPath      = test::makeScratchPath( "patch_big.pack" );
    const uint64       payloadOffset = 5ull * constant::memory::kBytesPerGigabyte / 2; // 2.5 GiB
    const string       entryPath     = "game/test/data/far.xml";
    const string       payload       = "far away";
    const uint32       payloadSize   = static_cast<uint32>( payload.size() );
    const uint8*       pPayload      = reinterpret_cast<const uint8*>( payload.data() );

    // 색인 구역: FAT · 블록 표 · 블록 해시 표 · 스트링 풀 · 헤더(쓰는 쪽과 같은 배치).
    PackFileEntryOnDisk fileEntry{};
    fileEntry._pathHash = HashUtil::computeStringHash64( entryPath );
    fileEntry.setPlacement( payloadOffset, PackEntryStorage::Blocks );
    fileEntry._compressedSize   = payloadSize;
    fileEntry._uncompressedSize = payloadSize;
    fileEntry._firstBlock       = 0;
    fileEntry._stringPoolOffset = 1; // 풀 맨 앞은 빈 마운트 도메인(NUL 하나)
    const PackBlockEntryOnDisk block{ payloadSize, HashUtil::computeCRC32( pPayload, payloadSize ) };
    PackDigest                 blockDigest;
    test::FakeNetSecurityProvider::computeFakeDigest( pPayload, static_cast<int32>( payloadSize ), blockDigest._arrByte );
    string pool( 1, '\0' );
    pool.append( entryPath );
    pool.push_back( '\0' );

    PackHeader header{};
    header._magic            = kPackMagic;
    header._formatVersion    = kPackFormatVersion;
    header._sectorAlignment  = kPackSectorAlignment;
    header._flags            = static_cast<uint16>( static_cast<uint16>( PackFlag::HasCRC32 ) | static_cast<uint16>( PackFlag::HasStringPool ) | static_cast<uint16>( PackFlag::HasBlockHash ) );
    header._fileCount        = 1;
    header._indexOffset      = payloadOffset + payloadSize;
    header._indexSize        = sizeof( PackFileEntryOnDisk );
    header._blockTableOffset = header._indexOffset + header._indexSize;
    header._blockHashOffset  = header._blockTableOffset + sizeof( PackBlockEntryOnDisk );
    header._blockCount       = 1;
    header._blockSize        = kPackMinBlockSize;
    header._stringPoolOffset = header._blockHashOffset + sizeof( PackDigest );
    header._stringPoolSize   = pool.size();
    header._totalDataSize    = header._indexOffset;
    vector<uint8> listIndexByte;
    const auto    appendIndex = [&listIndexByte]( const void* pData, size_t size )
    {
        const uint8* pByte = static_cast<const uint8*>( pData );
        listIndexByte.insert( listIndexByte.end(), pByte, pByte + size );
    };
    appendIndex( &fileEntry, sizeof( fileEntry ) );
    appendIndex( &block, sizeof( block ) );
    appendIndex( &blockDigest, sizeof( blockDigest ) );
    appendIndex( pool.data(), pool.size() );
    appendIndex( &header, sizeof( header ) );

    AtomicFileWriter file;
    SW_REQUIRE_TRUE( file.open( packPath ).has_value() );
    if ( file.markSparse() == false )
        SW_TEST_SKIP( "the scratch volume cannot hold sparse files - a 2.5 GiB pack would be written for real" ); // 소멸하는 쓰기가 임시 파일을 지운다
    SW_REQUIRE_TRUE( file.appendZeros( payloadOffset ).has_value() );                                             // 성긴 파일의 구멍 — 디스크를 쓰지 않는다
    SW_REQUIRE_TRUE( file.append( pPayload, payloadSize ).has_value() );
    SW_REQUIRE_TRUE( file.append( listIndexByte.data(), listIndexByte.size() ).has_value() );
    SW_REQUIRE_TRUE( file.commit().has_value() );

    PackIndexTrust trust{ &provider, {} };
    test::FakeNetSecurityProvider::computeFakeDigest( listIndexByte.data(), static_cast<int32>( listIndexByte.size() ), trust._indexDigest._arrByte );
    PackReader reader;
    SW_REQUIRE_TRUE( reader.open( packPath.str(), &trust ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( reader.readFile( entryPath, bytes ) );
    SW_EXPECT_TRUE( string_view( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() ) == payload );
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}
