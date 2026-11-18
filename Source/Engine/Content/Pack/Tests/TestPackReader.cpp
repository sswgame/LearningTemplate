#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Pack/PackReader.h"
#include "Engine/Content/Pack/Tests/PackTestUtil.h"

using namespace sw;

namespace
{
    /**
     * @brief 무압축 형식 3 팩을 바이트로 직접 짓습니다. 필드를 망가뜨려 손상 입력을 만듭니다.
     * @details `add` 는 항목을 모으기만 하고 `finalize` 가 경로 해시 순으로 세워 FAT · 블록 표 · 페이로드 · 풀을 짓는다(쓰는 쪽과 같은 규칙).
     *          망가뜨리는 테스트는 `finalize` 뒤의 칸을 바꾸므로 항목 번호는 해시 순서다 — 경로는 `getPath` 로 찾는다.
     */
    struct PackImage
    {
        PackHeader                        _header{};
        vector<PackFileEntryOnDisk>       _listEntry;
        vector<PackBlockEntryOnDisk>      _listBlock;
        vector<uint8>                     _listData;
        string                            _pool;
        vector<std::pair<string, string>> _listAdded;

        explicit PackImage( string_view domain = "game/test" )
        {
            _header._magic         = kPackMagic;
            _header._formatVersion = kPackFormatVersion;
            _header._flags         = static_cast<uint16>( PackFlag::HasStringPool ) | static_cast<uint16>( PackFlag::HasCRC32 );
            _header._blockSize     = kPackMinBlockSize;
            _pool.assign( domain );
            _pool.push_back( '\0' );
        }

        void add( string_view path, string_view content ) { _listAdded.emplace_back( string{ path }, string{ content } ); }

        /** @brief 모은 항목으로 구역을 짓고 머리를 채웁니다. 망가뜨리기 전에 부릅니다. */
        void finalize()
        {
            VectorUtil::sort( _listAdded, []( const std::pair<string, string>& left, const std::pair<string, string>& right )
            { return HashUtil::computeStringHash64( left.first ) < HashUtil::computeStringHash64( right.first ); } );
            for ( const std::pair<string, string>& added : _listAdded )
            {
                PackFileEntryOnDisk entry;
                entry._pathHash = HashUtil::computeStringHash64( added.first );
                entry.setPlacement( _listData.size(), PackEntryStorage::Blocks );
                entry._compressedSize   = static_cast<uint32>( added.second.size() );
                entry._uncompressedSize = static_cast<uint32>( added.second.size() );
                entry._firstBlock       = static_cast<uint32>( _listBlock.size() );
                entry._stringPoolOffset = static_cast<uint32>( _pool.size() );
                for ( size_t offset = 0; offset < added.second.size(); offset += kPackMinBlockSize )
                {
                    const size_t size = added.second.size() - offset < kPackMinBlockSize ? added.second.size() - offset : kPackMinBlockSize;
                    _listBlock.push_back( { static_cast<uint32>( offset + size ), HashUtil::computeCRC32( added.second.data() + offset, size ) } );
                }
                _listEntry.push_back( entry );
                _pool.append( added.first );
                _pool.push_back( '\0' );
                _listData.insert( _listData.end(), added.second.begin(), added.second.end() );
            }
            _header._fileCount        = static_cast<uint32>( _listEntry.size() );
            _header._totalDataSize    = _listData.size();
            _header._indexOffset      = _listData.size();
            _header._indexSize        = _listEntry.size() * sizeof( PackFileEntryOnDisk );
            _header._blockTableOffset = _header._indexOffset + _header._indexSize;
            _header._blockCount       = static_cast<uint32>( _listBlock.size() );
            _header._stringPoolOffset = _header._blockTableOffset + _listBlock.size() * sizeof( PackBlockEntryOnDisk );
            _header._stringPoolSize   = _pool.size();
        }

        /** @brief `finalize` 뒤 항목 @p index 의 경로입니다. */
        string getPath( size_t index ) const { return string{ _pool.c_str() + _listEntry[index]._stringPoolOffset }; }

        void write( string_view path ) const
        {
            vector<uint8> bytes;
            const auto    append = [&bytes]( const void* pData, size_t size )
            {
                const uint8* pByte = static_cast<const uint8*>( pData );
                bytes.insert( bytes.end(), pByte, pByte + size );
            };
            append( _listData.data(), _listData.size() );
            append( _listEntry.data(), _listEntry.size() * sizeof( PackFileEntryOnDisk ) );
            append( _listBlock.data(), _listBlock.size() * sizeof( PackBlockEntryOnDisk ) );
            append( _pool.data(), _pool.size() );
            append( &_header, sizeof( PackHeader ) );
            SW_REQUIRE_TRUE( FileUtil::writeFile( path, bytes.data(), bytes.size() ) );
        }
    };

    /** @brief 임시 팩 파일 하나입니다. 끝나면 지웁니다. */
    struct PackTestFile
    {
        string _path;

        PackTestFile()
            : _path{ test::makeScratchPath( "SwPackReaderTest.pack" ) }
        {
        }
        ~PackTestFile() { (void)FileUtil::removeFile( _path ); }

        /** @brief @p image 를 쓰고 여는 결과를 돌려줍니다. */
        bool open( const PackImage& image, PackReader& reader ) const
        {
            image.write( _path );
            return reader.open( _path );
        }
    };

    struct PackReaderTestUtil
    {
        static PackImage makeValidImage()
        {
            PackImage image;
            image.add( "maps/town.xml", "<Town/>" );
            image.add( "maps/deep/cave.xml", "<Cave/>" );
            image.add( "top.txt", "hello" );
            image.finalize();
            return image;
        }
    };
    using Util = PackReaderTestUtil;
} // namespace

// ---------------------------------------------------------------------- 읽기

SW_TEST_CASE( PackReaderTest, ReadsEntriesAndHeader )
{
    const PackTestFile file;
    PackReader         reader;
    SW_REQUIRE_TRUE( file.open( Util::makeValidImage(), reader ) );
    SW_EXPECT_TRUE( reader.isOpen() );
    SW_EXPECT_EQUAL( 3u, reader.getFileCount() );
    SW_EXPECT_TRUE( reader.getMountDomain() == "game/test" );
    SW_EXPECT_TRUE( reader.hasFile( "maps/town.xml" ) );
    SW_EXPECT_FALSE( reader.hasFile( "maps/nope.xml" ) );

    string text;
    SW_REQUIRE_TRUE( reader.readTextFile( "maps/deep/cave.xml", text ) );
    SW_EXPECT_TRUE( text == "<Cave/>" );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( reader.readFile( "top.txt", bytes ) );
    SW_EXPECT_EQUAL( size_t{ 5 }, bytes.size() );
    SW_EXPECT_FALSE( reader.readFile( "maps/nope.xml", bytes ) );

    PackFileEntry entry;
    SW_REQUIRE_TRUE( reader.getFileEntry( "top.txt", entry ) );
    SW_EXPECT_TRUE( entry._relativePath == "top.txt" );

    reader.close();
    SW_EXPECT_FALSE( reader.isOpen() );
    SW_EXPECT_FALSE( reader.hasFile( "top.txt" ) );
    SW_EXPECT_TRUE( reader.getMountDomain().empty() );
}

SW_TEST_CASE( PackReaderTest, TextFilesLoseTheirByteOrderMark )
{
    PackImage image;
    image.add( "bom.txt", "\xEF\xBB\xBFtext" );
    image.finalize();
    const PackTestFile file;
    PackReader         reader;
    SW_REQUIRE_TRUE( file.open( image, reader ) );
    string text;
    SW_REQUIRE_TRUE( reader.readTextFile( "bom.txt", text ) );
    SW_EXPECT_TRUE( text == "text" );
}

// 폴더 나열은 그 폴더의 항목만이고 대소문자를 보지 않는다. 아래 폴더는 들지 않는다.
SW_TEST_CASE( PackReaderTest, FolderListingHoldsOnlyItsOwnEntries )
{
    const PackTestFile file;
    PackReader         reader;
    SW_REQUIRE_TRUE( file.open( Util::makeValidImage(), reader ) );
    vector<string> listPath;
    reader.collectFolderEntries( "MAPS", listPath );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listPath.size() );
    SW_EXPECT_TRUE( listPath[0] == "maps/town.xml" );
    listPath.clear();
    reader.collectFolderEntries( "", listPath );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listPath.size() );
    SW_EXPECT_TRUE( listPath[0] == "top.txt" );
    listPath.clear();
    reader.collectFolderEntries( "nowhere", listPath );
    SW_EXPECT_TRUE( listPath.empty() );
    reader.collectAllEntryPaths( listPath );
    SW_EXPECT_EQUAL( size_t{ 3 }, listPath.size() );
}

SW_TEST_CASE( PackReaderTest, EmptyPackStillHasItsDomain )
{
    PackImage image;
    image.finalize();
    const PackTestFile file;
    PackReader         reader;
    SW_REQUIRE_TRUE( file.open( image, reader ) );
    SW_EXPECT_EQUAL( 0u, reader.getFileCount() );
    SW_EXPECT_TRUE( reader.getMountDomain() == "game/test" );
}

SW_TEST_CASE( PackReaderTest, MovedReaderKeepsTheFile )
{
    const PackTestFile file;
    PackReader         reader;
    SW_REQUIRE_TRUE( file.open( Util::makeValidImage(), reader ) );
    PackReader moved = std::move( reader );
    SW_EXPECT_TRUE( moved.hasFile( "top.txt" ) );
    vector<uint8> bytes;
    SW_EXPECT_TRUE( moved.readFile( "top.txt", bytes ) );
}

SW_TEST_CASE( PackReaderTest, OpeningAgainReplacesTheOldContents )
{
    const PackTestFile file;
    PackReader         reader;
    SW_REQUIRE_TRUE( file.open( Util::makeValidImage(), reader ) );
    PackImage other( "game/other" );
    other.add( "only.txt", "x" );
    other.finalize();
    SW_REQUIRE_TRUE( file.open( other, reader ) );
    SW_EXPECT_FALSE( reader.hasFile( "top.txt" ) );
    SW_EXPECT_TRUE( reader.hasFile( "only.txt" ) );
    SW_EXPECT_TRUE( reader.getMountDomain() == "game/other" );
}

// ---------------------------------------------------------------------- 손상된 머리

SW_TEST_CASE( PackReaderTest, MissingFileIsAnError )
{
    SW_EXPECT_LOG( "PackReader.FailedOpenResourcePack" );
    PackReader reader;
    SW_EXPECT_FALSE( reader.open( test::makeScratchPath( "SwNoSuchPack.pack" ) ) );
    SW_EXPECT_FALSE( reader.open( "" ) );
    SW_EXPECT_FALSE( reader.isOpen() );
}

SW_TEST_CASE( PackReaderTest, WrongMagicAndVersionAreRefused )
{
    SW_EXPECT_LOG( "PackReader.InvalidPackMagicX" );
    SW_EXPECT_LOG( "PackReader.UnsupportedPackFormatVersion" );
    const PackTestFile file;
    PackReader         reader;
    PackImage          image = Util::makeValidImage();
    image._header._magic     = 0x12345678u;
    SW_EXPECT_FALSE( file.open( image, reader ) );
    SW_EXPECT_FALSE( reader.isOpen() );
    image                        = Util::makeValidImage();
    image._header._formatVersion = kPackFormatVersion + 1;
    SW_EXPECT_FALSE( file.open( image, reader ) );
}

// 풀 방법이 없으므로 평문으로 풀지 않고 거절한다.
SW_TEST_CASE( PackReaderTest, EncryptedPacksAreRefused )
{
    SW_EXPECT_LOG( "PackReader.EncryptedPacksNotSupported" );
    const PackTestFile file;
    PackReader         reader;
    PackImage          image = Util::makeValidImage();
    image._header._flags     = static_cast<uint16>( image._header._flags | static_cast<uint16>( PackFlag::Encrypted ) );
    SW_EXPECT_FALSE( file.open( image, reader ) );
}

SW_TEST_CASE( PackReaderTest, ShortFileIsRefused )
{
    SW_EXPECT_LOG( "PackReader.CorruptedPackHeader" );
    const PackTestFile file;
    const uint8        arrShort[4] = { 1, 2, 3, 4 };
    SW_REQUIRE_TRUE( FileUtil::writeFile( file._path, arrShort, sizeof( arrShort ) ) );
    PackReader reader;
    SW_EXPECT_FALSE( reader.open( file._path ) );
}

// 머리가 말하는 크기를 믿고 할당하지 않는다.
SW_TEST_CASE( PackReaderTest, HeaderGeometryIsChecked )
{
    SW_EXPECT_LOG( "PackReader.FailedLoadFATIndex" );
    SW_EXPECT_LOG( "PackReader.PackIndexSizeDisagrees" );
    SW_EXPECT_LOG( "PackReader.PackIndexTableLies" );
    SW_EXPECT_LOG( "PackReader.PackStringPoolLies" );
    SW_EXPECT_LOG( "PackReader.PackNoEntryPath" );
    const PackTestFile file;
    PackReader         reader;

    PackImage disagrees          = Util::makeValidImage();
    disagrees._header._indexSize = 7;
    SW_EXPECT_FALSE( file.open( disagrees, reader ) );

    PackImage outsideIndex          = Util::makeValidImage();
    outsideIndex._header._fileCount = 1000000;
    outsideIndex._header._indexSize = 1000000ull * sizeof( PackFileEntryOnDisk );
    SW_EXPECT_FALSE( file.open( outsideIndex, reader ) );

    PackImage overflowIndex            = Util::makeValidImage();
    overflowIndex._header._indexOffset = ~0ull - 8; // `offset + size` 가 넘친다
    SW_EXPECT_FALSE( file.open( overflowIndex, reader ) );

    PackImage outsidePool               = Util::makeValidImage();
    outsidePool._header._stringPoolSize = 1ull << 40;
    SW_EXPECT_FALSE( file.open( outsidePool, reader ) );

    PackImage noPool      = Util::makeValidImage();
    noPool._header._flags = static_cast<uint16>( PackFlag::HasCRC32 );
    SW_EXPECT_FALSE( file.open( noPool, reader ) );
    SW_EXPECT_FALSE( reader.isOpen() );
}

SW_TEST_CASE( PackReaderTest, PoolWithoutADomainIsRefused )
{
    SW_EXPECT_LOG( "PackReader.PackNoMountDomain" );
    SW_EXPECT_LOG( "PackReader.FailedLoadFATIndex" );
    PackImage image;
    image._pool = "no terminator";
    image.finalize();
    const PackTestFile file;
    PackReader         reader;
    SW_EXPECT_FALSE( file.open( image, reader ) );
}

// ---------------------------------------------------------------------- 손상된 항목

// 손상된 32 바이트 항목 하나가 4 GB 할당 요청이 되면 안 된다.
SW_TEST_CASE( PackReaderTest, EntryPayloadMustLieInsideTheFile )
{
    SW_EXPECT_LOG( "PackReader.PackEntryPayloadLies" );
    SW_EXPECT_LOG( "PackReader.FailedLoadFATIndex" );
    const PackTestFile file;
    PackReader         reader;
    PackImage          farOffset = Util::makeValidImage();
    farOffset._listEntry[0].setPlacement( 1ull << 40, PackEntryStorage::Blocks );
    SW_EXPECT_FALSE( file.open( farOffset, reader ) );

    PackImage hugeSize                       = Util::makeValidImage();
    hugeSize._listEntry[0]._compressedSize   = 0xFFFFFFFFu;
    hugeSize._listEntry[0]._uncompressedSize = 0xFFFFFFFFu;
    SW_EXPECT_FALSE( file.open( hugeSize, reader ) );
}

// 블록 줄은 항목 크기 · 앞 항목의 줄과 맞아야 한다 — 읽기가 블록 표의 자리를 그대로 믿는다.
SW_TEST_CASE( PackReaderTest, BlocksMustMatchTheEntry )
{
    SW_EXPECT_LOG( "PackReader.PackEntryBlocksDisagree" );
    SW_EXPECT_LOG( "PackReader.PackBlockTableDisagrees" );
    SW_EXPECT_LOG( "PackReader.PackBlockSizeInvalid" );
    SW_EXPECT_LOG( "PackReader.FailedLoadFATIndex" );
    const PackTestFile file;
    PackReader         reader;
    PackImage          biggerRaw              = Util::makeValidImage();
    biggerRaw._listEntry[0]._uncompressedSize = 0x00100000u; // 블록 줄이 모자란다
    SW_EXPECT_FALSE( file.open( biggerRaw, reader ) );

    PackImage skipped                 = Util::makeValidImage();
    skipped._listEntry[1]._firstBlock = 0; // 앞 항목의 줄과 겹친다
    SW_EXPECT_FALSE( file.open( skipped, reader ) );

    PackImage spareBlock = Util::makeValidImage();
    spareBlock._listBlock.push_back( { 1u, 0u } );
    spareBlock._header._blockCount = static_cast<uint32>( spareBlock._listBlock.size() );
    spareBlock._header._stringPoolOffset += sizeof( PackBlockEntryOnDisk );
    SW_EXPECT_FALSE( file.open( spareBlock, reader ) );

    PackImage oddBlockSize          = Util::makeValidImage();
    oddBlockSize._header._blockSize = kPackMinBlockSize + 1;
    SW_EXPECT_FALSE( file.open( oddBlockSize, reader ) );
}

SW_TEST_CASE( PackReaderTest, EntryPathMustLieInThePoolAndMatchItsHash )
{
    SW_EXPECT_LOG( "PackReader.PackEntryPathLies" );
    SW_EXPECT_LOG( "PackReader.PackEntryPathDoes" );
    SW_EXPECT_LOG( "PackReader.FailedLoadFATIndex" );
    const PackTestFile file;
    PackReader         reader;
    PackImage          outside              = Util::makeValidImage();
    outside._listEntry[0]._stringPoolOffset = 0xFFFFFFF0u;
    SW_EXPECT_FALSE( file.open( outside, reader ) );

    PackImage wrongHash = Util::makeValidImage();
    wrongHash._listEntry[0]._pathHash ^= 1u;
    SW_EXPECT_FALSE( file.open( wrongHash, reader ) );
}

// 체크섬이 다른 항목은 내주지 않는다. 저장된 값을 0 으로 지워도 검사가 꺼지지 않는다.
SW_TEST_CASE( PackReaderTest, CorruptedPayloadFailsTheChecksum )
{
    SW_EXPECT_LOG( "PackReader.CRC32ChecksumMismatchPack" );
    const PackTestFile file;
    PackReader         reader;
    PackImage          image = Util::makeValidImage();
    image._listData[0] ^= 0xFFu; // 해시 순 첫 항목의 첫 바이트
    SW_REQUIRE_TRUE( file.open( image, reader ) );
    vector<uint8> bytes;
    SW_EXPECT_FALSE( reader.readFile( image.getPath( 0 ), bytes ) );
    SW_EXPECT_TRUE( bytes.empty() );
    SW_EXPECT_TRUE( reader.readFile( image.getPath( 1 ), bytes ) ); // 다른 항목은 읽힌다

    PackImage zeroed            = Util::makeValidImage();
    zeroed._listBlock[0]._crc32 = 0;
    SW_REQUIRE_TRUE( file.open( zeroed, reader ) );
    SW_EXPECT_FALSE( reader.readFile( zeroed.getPath( 0 ), bytes ) );
}

SW_TEST_CASE( PackReaderTest, ChecksumIsOptionalByFlag )
{
    const PackTestFile file;
    PackReader         reader;
    PackImage          image = Util::makeValidImage();
    image._header._flags     = static_cast<uint16>( PackFlag::HasStringPool );
    image._listData[0] ^= 0xFFu;
    SW_REQUIRE_TRUE( file.open( image, reader ) );
    vector<uint8> bytes;
    SW_EXPECT_TRUE( reader.readFile( image.getPath( 0 ), bytes ) );
}

// ---------------------------------------------------------------------- 정렬 배열 색인 · 범위 읽기

// 리더는 FAT 을 그대로 들고 이진 탐색한다 — 경로 해시 오름차순이 아니거나 같은 해시가 둘이면 열지 않는다.
SW_TEST_CASE( PackReaderTest, UnsortedIndexIsRefused )
{
    SW_EXPECT_LOG( "PackReader.UnsortedIndex" );
    SW_EXPECT_LOG( "PackReader.FailedLoadFATIndex" );
    const PackTestFile file;
    PackReader         reader;
    PackImage          swapped = Util::makeValidImage();
    std::swap( swapped._listEntry[0], swapped._listEntry[1] );
    SW_EXPECT_FALSE( file.open( swapped, reader ) );
    SW_EXPECT_FALSE( reader.isOpen() );

    PackImage duplicated               = Util::makeValidImage();
    duplicated._listEntry[1]._pathHash = duplicated._listEntry[0]._pathHash;
    SW_EXPECT_FALSE( file.open( duplicated, reader ) );
}

// 범위 읽기는 걸친 블록만 읽어 풀고, 결과는 통째 읽기에서 같은 범위를 자른 것과 같다. 항목 밖 범위는 오류다.
SW_TEST_CASE( PackReaderTest, RangeReadEqualsTheSameSliceOfAWholeRead )
{
    const string  packPath  = test::makeScratchPath( "SwPackReaderRange.pack" );
    const uint32  blockSize = PackTestUtil::kBlockSize;
    vector<uint8> content( 4 * blockSize + 777 );
    for ( size_t index = 0; index < content.size(); ++index )
    {
        content[index] = static_cast<uint8>( index < content.size() / 2 ? ( index * 31 ) ^ ( index >> 9 ) : index / 4096 );
    }
    vector<std::pair<string, vector<uint8>>> listEntry;
    listEntry.emplace_back( "textures/mips.dds", content );
    listEntry.emplace_back( "small.txt", vector<uint8>{ 'h', 'i' } );
    SW_REQUIRE_TRUE( PackTestUtil::writePack( packPath, PackTestUtil::makeDesc( "game/test", PackCompressionKind::Zlib ), listEntry ) );

    PackReader reader;
    SW_REQUIRE_TRUE( reader.open( packPath ) );
    const uint64  pathHash = HashUtil::computeStringHash64( string_view( "textures/mips.dds" ) );
    vector<uint8> whole;
    SW_REQUIRE_TRUE( reader.readFile( pathHash, whole ) );
    SW_REQUIRE_TRUE( whole == content );

    const uint32                    size       = static_cast<uint32>( content.size() );
    const std::pair<uint32, uint32> arrRange[] = {
        {                0,             1},
        {                0,     blockSize},
        {    blockSize - 1,             2},
        {        blockSize,     blockSize},
        {              100, 3 * blockSize},
        {         size - 7,             7},
        {             size,             0},
        {                0,          size},
        {2 * blockSize + 5,             0},
    };
    for ( const std::pair<uint32, uint32>& range : arrRange )
    {
        vector<uint8> slice;
        SW_REQUIRE_TRUE( reader.readRange( pathHash, range.first, range.second, slice ) );
        SW_EXPECT_TRUE( slice == vector<uint8>( whole.begin() + range.first, whole.begin() + range.first + range.second ) );
    }

    SW_EXPECT_LOG( "PackReader.RangeOutsideEntry" );
    vector<uint8> outside;
    SW_EXPECT_FALSE( reader.readRange( pathHash, size - 1, 2, outside ) );
    SW_EXPECT_FALSE( reader.readRange( pathHash, uint64{ size } + 1, 0, outside ) );
    SW_EXPECT_FALSE( reader.readRange( HashUtil::computeStringHash64( string_view( "nope.dds" ) ), 0, 1, outside ) );
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}

// 블록 하나가 망가지면 그 블록에 걸친 읽기만 거절한다 — 다른 블록의 범위 읽기는 된다.
SW_TEST_CASE( PackReaderTest, CorruptBlockIsRefused )
{
    SW_EXPECT_LOG( "PackReader.CRC32ChecksumMismatchPack" );
    string content( 3 * kPackMinBlockSize + 10, 'a' );
    for ( size_t index = 0; index < content.size(); ++index )
    {
        content[index] = static_cast<utf8>( 'a' + index % 26 );
    }
    PackImage image;
    image.add( "big.bin", content );
    image.finalize();
    image._listData[2 * kPackMinBlockSize + 5] ^= 0x5Au; // 블록 2 안
    const PackTestFile file;
    PackReader         reader;
    SW_REQUIRE_TRUE( file.open( image, reader ) );
    const uint64  pathHash = HashUtil::computeStringHash64( string_view( "big.bin" ) );
    vector<uint8> bytes;
    SW_EXPECT_FALSE( reader.readFile( pathHash, bytes ) );
    SW_EXPECT_TRUE( bytes.empty() );
    SW_EXPECT_FALSE( reader.readRange( pathHash, 2 * kPackMinBlockSize, 1, bytes ) );
    SW_EXPECT_FALSE( reader.readRange( pathHash, kPackMinBlockSize, 2 * kPackMinBlockSize, bytes ) ); // 블록 1 · 2 에 걸친다
    SW_REQUIRE_TRUE( reader.readRange( pathHash, 10, kPackMinBlockSize, bytes ) );                    // 블록 0 · 1
    SW_EXPECT_TRUE( bytes.size() == kPackMinBlockSize && bytes[0] == static_cast<uint8>( content[10] ) );
    SW_EXPECT_TRUE( reader.readRange( pathHash, 3 * kPackMinBlockSize, 10, bytes ) ); // 블록 3
}

// ---------------------------------------------------------------------- 항목 저장 방식

// `Stored` 항목은 팩 코덱과 상관없이 블록의 저장 바이트가 원본이다 — 범위 읽기가 풀기 없이 같은 구간을 준다. 같은 팩의 `Blocks` 항목은 그대로 풀린다.
SW_TEST_CASE( PackReaderTest, StoredEntryRangeReadReturnsTheSameSpan )
{
    const string  packPath  = test::makeScratchPath( "SwPackReaderStored.pack" );
    const uint32  blockSize = PackTestUtil::kBlockSize;
    vector<uint8> stored( 2 * blockSize + 321 );
    for ( size_t index = 0; index < stored.size(); ++index )
    {
        stored[index] = static_cast<uint8>( ( index * 131 ) ^ ( index >> 7 ) );
    }
    const vector<uint8> compressed( 3 * blockSize, uint8{ 'z' } );
    const string        storedPath     = "textures/rock.dds";
    const string        compressedPath = "data/level.xml";
    const bool          bStoredFirst   = PackStreamWriter::makePathHash( storedPath ) < PackStreamWriter::makePathHash( compressedPath );
    PackStreamWriter    writer;
    SW_REQUIRE_TRUE( writer.open( packPath, PackTestUtil::makeDesc( "game/test", PackCompressionKind::Zlib ) ).has_value() );
    for ( uint32 order = 0; order < 2; ++order )
    {
        const bool bStored = ( order == 0 ) == bStoredFirst;
        SW_REQUIRE_TRUE( writer.append( bStored ? storedPath : compressedPath, bStored ? PackEntryStorage::Stored : PackEntryStorage::Blocks, bStored ? stored : compressed ).has_value() );
    }
    SW_REQUIRE_TRUE( writer.close().has_value() );

    PackReader reader;
    SW_REQUIRE_TRUE( reader.open( packPath ) );
    PackFileEntry entry;
    SW_REQUIRE_TRUE( reader.getFileEntry( storedPath, entry ) );
    SW_EXPECT_TRUE( entry._storage == PackEntryStorage::Stored );
    SW_EXPECT_EQUAL( entry._uncompressedSize, entry._compressedSize ); // 압축하지 않았다
    SW_REQUIRE_TRUE( reader.getFileEntry( compressedPath, entry ) );
    SW_EXPECT_TRUE( entry._storage == PackEntryStorage::Blocks && entry._compressedSize < entry._uncompressedSize );

    const uint64  storedHash = HashUtil::computeStringHash64( storedPath );
    vector<uint8> whole;
    SW_REQUIRE_TRUE( reader.readFile( storedHash, whole ) );
    SW_REQUIRE_TRUE( whole == stored );
    const std::pair<uint32, uint32> arrRange[] = {
        {                                       0,       128},
        {                           blockSize - 3,         6},
        {                          blockSize + 10, blockSize},
        {static_cast<uint32>( stored.size() ) - 9,         9}
    };
    for ( const std::pair<uint32, uint32>& range : arrRange )
    {
        vector<uint8> slice;
        SW_REQUIRE_TRUE( reader.readRange( storedHash, range.first, range.second, slice ) );
        SW_EXPECT_TRUE( slice == vector<uint8>( stored.begin() + range.first, stored.begin() + range.first + range.second ) );
    }
    SW_REQUIRE_TRUE( reader.readFile( compressedPath, whole ) );
    SW_EXPECT_TRUE( whole == compressed );
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}

// 저장 방식은 디스크 값이다 — 모르는 값의 항목이 있는 팩은 열지 않는다.
SW_TEST_CASE( PackReaderTest, UnknownEntryStorageIsRefused )
{
    SW_EXPECT_LOG( "PackReader.UnknownEntryStorage" );
    SW_EXPECT_LOG( "PackReader.FailedLoadFATIndex" );
    const PackTestFile file;
    PackReader         reader;
    PackImage          image = Util::makeValidImage();
    image._listEntry[0].setPlacement( image._listEntry[0].getDataOffset(), static_cast<PackEntryStorage>( kPackEntryStorageCount ) );
    SW_EXPECT_FALSE( file.open( image, reader ) );
    SW_EXPECT_FALSE( reader.isOpen() );
}

// ---------------------------------------------------------------------- 구간 읽기(텍스처 밉 범위 — MR-1)

/** @brief [PackReaderTest] 비동기 구간 읽기는 통째 읽기의 같은 구간이고, 항목 밖 구간은 오류다 */
SW_TEST_CASE( PackReaderTest, RangeReadOnUncompressedEntryMatchesWholeRead )
{
    SW_EXPECT_LOG( "PackReader.RangeOutsideEntry" );
    const PackTestFile file;
    PackReader         reader;
    SW_REQUIRE_TRUE( file.open( Util::makeValidImage(), reader ) );
    AsyncFileIO io;
    SW_REQUIRE_TRUE( io.initialize( AsyncFileIOConfig{} ) );

    vector<uint8>   range;
    bool            bSucceeded = false;
    AsyncReadHandle read       = reader.readRangeAsync( io, HashUtil::computeStringHash64( string_view( "maps/deep/cave.xml" ) ), 1, 4, AsyncIOPriority::Normal,
                                                        SW_DELEGATE_LAMBDA( AssetReadCompleteDelegate, [&range, &bSucceeded]( bool bSuccess, vector<uint8>& bytes )
          {
        bSucceeded = bSuccess;
        range      = std::move( bytes );
    } ) );
    SW_REQUIRE_TRUE( read.isValid() );
    read.wait();
    SW_EXPECT_TRUE( bSucceeded );
    SW_EXPECT_TRUE( string( range.begin(), range.end() ) == "Cave" );

    SW_EXPECT_FALSE( reader.readRangeAsync( io, HashUtil::computeStringHash64( string_view( "top.txt" ) ), 3, 3, AsyncIOPriority::Normal, {} ).isValid() ); // 5 바이트 항목의 3+3
    io.shutdown();
}
