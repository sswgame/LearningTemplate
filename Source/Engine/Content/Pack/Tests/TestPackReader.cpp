#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Pack/PackReader.h"

using namespace sw;

namespace
{
    /** @brief 무압축 팩을 바이트로 직접 짓습니다. 필드를 망가뜨려 손상 입력을 만듭니다. */
    struct PackImage
    {
        PackHeader                  _header{};
        vector<PackFileEntryOnDisk> _listEntry;
        vector<uint8>               _listData;
        string                      _pool;

        explicit PackImage( string_view domain = "game/test" )
        {
            _header._magic         = kPackMagic;
            _header._formatVersion = kPackFormatVersion;
            _header._flags         = static_cast<uint16>( PackFlag::HasStringPool ) | static_cast<uint16>( PackFlag::HasCRC32 );
            _pool.assign( domain );
            _pool.push_back( '\0' );
        }

        void add( string_view path, string_view content )
        {
            PackFileEntryOnDisk entry;
            entry._pathHash         = HashUtil::computeStringHash64( path );
            entry._dataOffset       = sizeof( PackHeader ) + _listData.size();
            entry._compressedSize   = static_cast<uint32>( content.size() );
            entry._uncompressedSize = static_cast<uint32>( content.size() );
            entry._crc32            = HashUtil::computeCRC32( reinterpret_cast<const uint8*>( content.data() ), content.size() );
            entry._stringPoolOffset = static_cast<uint32>( _pool.size() );
            _listEntry.push_back( entry );
            _pool.append( path.data(), path.size() );
            _pool.push_back( '\0' );
            _listData.insert( _listData.end(), content.begin(), content.end() );
        }

        /** @brief 머리의 구역 위치를 채웁니다. 망가뜨리기 전에 부릅니다. */
        void finalize()
        {
            _header._fileCount        = static_cast<uint32>( _listEntry.size() );
            _header._stringPoolOffset = sizeof( PackHeader ) + _listData.size();
            _header._stringPoolSize   = _pool.size();
            _header._indexOffset      = _header._stringPoolOffset + _pool.size();
            _header._indexSize        = _listEntry.size() * sizeof( PackFileEntryOnDisk );
            _header._totalDataSize    = _listData.size();
        }

        void write( string_view path ) const
        {
            vector<uint8> bytes;
            const auto    append = [&bytes]( const void* pData, size_t size )
            {
                const uint8* pByte = static_cast<const uint8*>( pData );
                bytes.insert( bytes.end(), pByte, pByte + size );
            };
            append( &_header, sizeof( PackHeader ) );
            append( _listData.data(), _listData.size() );
            append( _pool.data(), _pool.size() );
            append( _listEntry.data(), _listEntry.size() * sizeof( PackFileEntryOnDisk ) );
            SW_REQUIRE_TRUE( FileUtil::writeFile( path, bytes.data(), bytes.size() ) );
        }
    };

    /** @brief 임시 팩 파일 하나입니다. 끝나면 지웁니다. */
    struct PackTestFile
    {
        string _path;

        PackTestFile()
            : _path{ FileUtil::joinPath( FileUtil::getTempDirectory(), "SwPackReaderTest.pack" ) }
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
    SW_EXPECT_FALSE( reader.open( FileUtil::joinPath( FileUtil::getTempDirectory(), "SwNoSuchPack.pack" ) ) );
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
    PackImage          farOffset        = Util::makeValidImage();
    farOffset._listEntry[0]._dataOffset = 1ull << 40;
    SW_EXPECT_FALSE( file.open( farOffset, reader ) );

    PackImage hugeSize                       = Util::makeValidImage();
    hugeSize._listEntry[0]._compressedSize   = 0xFFFFFFFFu;
    hugeSize._listEntry[0]._uncompressedSize = 0xFFFFFFFFu;
    SW_EXPECT_FALSE( file.open( hugeSize, reader ) );

    PackImage hugeRaw                       = Util::makeValidImage();
    hugeRaw._listEntry[0]._uncompressedSize = 0x7FFFFFFFu; // 무압축은 원본 크기만큼 파일에서 읽는다
    SW_EXPECT_FALSE( file.open( hugeRaw, reader ) );
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
    image._listData[0] ^= 0xFFu;
    SW_REQUIRE_TRUE( file.open( image, reader ) );
    vector<uint8> bytes;
    SW_EXPECT_FALSE( reader.readFile( "maps/town.xml", bytes ) );
    SW_EXPECT_TRUE( bytes.empty() );
    SW_EXPECT_TRUE( reader.readFile( "top.txt", bytes ) ); // 다른 항목은 읽힌다

    PackImage zeroed            = Util::makeValidImage();
    zeroed._listEntry[0]._crc32 = 0;
    SW_REQUIRE_TRUE( file.open( zeroed, reader ) );
    SW_EXPECT_FALSE( reader.readFile( "maps/town.xml", bytes ) );
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
    SW_EXPECT_TRUE( reader.readFile( "maps/town.xml", bytes ) );
}
