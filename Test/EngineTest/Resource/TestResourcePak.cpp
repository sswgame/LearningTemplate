#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/pair.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/Pack/ResourcePakCompressionUtil.h"
#include "Engine/Resource/Pack/ResourcePakManager.h"
#include "Engine/Resource/Pack/ResourcePakReader.h"
#include "Engine/Resource/Pack/ResourcePakTypes.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/ResourcePakTestUtil.h"

#include "TestFramework/TestFramework.h"

#include <atomic>
#include <functional>
#include <thread>

namespace sw
{
    namespace
    {
        /**
         * @struct GlobalVfsScope
         * @brief 전역 VFS 를 헤집는 테스트가 끝날 때 시작 시점 마운트로 되돌립니다.
         * @details 이 파일의 테스트들은 우선순위·오버라이드를 보려고 `unmountAll()` 로 판을 비운다.
         *          그런데 그 판은 **프로세스 전체가 쓰는 것**이라, 되돌리지 않으면 뒤에 도는 테스트가
         *          팩을 통째로 잃는다. Dev 에서는 느슨한 `Resource/` 트리가 가려 주지만 배포본은 팩이
         *          전부다 — 쿠킹된 씬 바이너리를 찾는 뒤 테스트(`SceneTest.EditorTestSceneResolvesMovedPrefabByGuid` 등)이
         *          **Shipping 에서만** 지고, 단독으로 돌리면 통과해 원인을 짚기 어렵다. 검색 우선순위도 같은 이유로 되돌린다.
         */
        struct GlobalVfsScope
        {
            GlobalVfsScope()
                : _listSearchPriority{ ResourcePaths::getSearchPriority() }
            {
            }

            ~GlobalVfsScope()
            {
                ResourcePakManager& packManager = ResourceIO::getPackManager();
                packManager.unmountAll();
                packManager.setAllowLooseFiles( true );
                ResourcePaths::setSearchPriority( _listSearchPriority );
                engine::getSubsystem<AssetManager>().mountStartupPacks();
                engine::getSubsystem<AssetManager>().loadAssetRegistries();
            }

            GlobalVfsScope( const GlobalVfsScope& )            = delete;
            GlobalVfsScope& operator=( const GlobalVfsScope& ) = delete;

        private:
            vector<string> _listSearchPriority;
        };

        /** @brief 폴더 나열 테스트가 쓰는 팩 항목 하나입니다(본문은 경로 그대로 — 읽어서 어느 항목인지 맞춘다). */
        pair<string, string> makeListingEntry( const string& path )
        {
            return { path, path };
        }

        /** @brief 같은 키를 텍스트 · 바이너리로 둘 다 읽습니다. 둘 다 읽었으면 true. */
        [[nodiscard]] bool readTextAndBinaryResource( const utf8* pKey, string& outText, string& outBinary )
        {
            vector<uint8> bytes;
            const bool    bText   = ResourceIO::readTextResource( pKey, outText );
            const bool    bBinary = ResourceIO::readBinaryResource( pKey, bytes );
            outBinary.assign( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
            return bText && bBinary;
        }

        /** @brief 이미 만들어진 팩 파일의 헤더를 읽고(있는 그대로) 다시 쓰는 테스트 헬퍼. */
        [[nodiscard]] bool readPackHeaderFromDisk( const string& packPath, PackHeader& outHeader )
        {
            vector<uint8> bytes;
            if ( FileUtil::readFile( packPath, bytes ) == false || bytes.size() < sizeof( PackHeader ) )
                return false;
            Memory::copy( &outHeader, bytes.data(), sizeof( PackHeader ) );
            return true;
        }

        /** @brief 팩의 FAT 항목 하나를 읽습니다. 헤더가 적어 둔 인덱스 오프셋을 씁니다. */
        [[nodiscard]] bool readPackEntryFromDisk( const string& packPath, uint32 entryIndex, PackFileEntryOnDisk& outEntry )
        {
            PackHeader header{};
            if ( readPackHeaderFromDisk( packPath, header ) == false || entryIndex >= header._fileCount )
                return false;

            vector<uint8> bytes;
            if ( FileUtil::readFile( packPath, bytes ) == false )
                return false;

            const size_t at = static_cast<size_t>( header._indexOffset ) + entryIndex * sizeof( PackFileEntryOnDisk );
            if ( at + sizeof( PackFileEntryOnDisk ) > bytes.size() )
                return false;
            Memory::copy( &outEntry, bytes.data() + at, sizeof( PackFileEntryOnDisk ) );
            return true;
        }

        /** @brief 팩의 FAT 항목 하나를 덮어씁니다. */
        [[nodiscard]] bool writePackEntryToDisk( const string& packPath, uint32 entryIndex, const PackFileEntryOnDisk& entry )
        {
            PackHeader header{};
            if ( readPackHeaderFromDisk( packPath, header ) == false || entryIndex >= header._fileCount )
                return false;

            vector<uint8> bytes;
            if ( FileUtil::readFile( packPath, bytes ) == false )
                return false;

            const size_t at = static_cast<size_t>( header._indexOffset ) + entryIndex * sizeof( PackFileEntryOnDisk );
            if ( at + sizeof( PackFileEntryOnDisk ) > bytes.size() )
                return false;
            Memory::copy( bytes.data() + at, &entry, sizeof( PackFileEntryOnDisk ) );
            return FileUtil::writeFile( packPath, bytes.data(), static_cast<uint64>( bytes.size() ) );
        }

        [[nodiscard]] bool writePackHeaderToDisk( const string& packPath, const PackHeader& header )
        {
            vector<uint8> bytes;
            if ( FileUtil::readFile( packPath, bytes ) == false || bytes.size() < sizeof( PackHeader ) )
                return false;
            Memory::copy( bytes.data(), &header, sizeof( PackHeader ) );
            return FileUtil::writeFile( packPath, bytes.data(), static_cast<uint64>( bytes.size() ) );
        }

        /** @brief 파일 @p path 의 @p offset 에 @p byteCount 바이트를 덮어씁니다(팩 변조 시뮬레이션). */
        bool patchFileBytesInternal( const sw::string& path, uint64 offset, const void* pBytes, size_t byteCount )
        {
            sw::vector<uint8> bytes;
            if ( sw::FileUtil::readFile( path, bytes ) == false || offset + byteCount > bytes.size() )
                return false;
            sw::Memory::copy( bytes.data() + offset, pBytes, byteCount );
            return sw::FileUtil::writeFile( path, bytes.data(), bytes.size() );
        }

        /** @brief 팩 헤더에서 FAT 시작 오프셋을 읽습니다(PackHeader::_indexOffset, offset 22). */
        uint64 readIndexOffsetInternal( const sw::string& path )
        {
            sw::vector<uint8> bytes;
            if ( sw::FileUtil::readFile( path, bytes ) == false || bytes.size() < 30 )
                return 0;
            uint64 indexOffset{ 0 };
            sw::Memory::copy( &indexOffset, bytes.data() + 22, sizeof( indexOffset ) );
            return indexOffset;
        }

        /** @brief RLE 를 그대로 위임하고 해제 횟수만 세는 코덱입니다. 팩 리더가 레지스트리의 코덱을 쓰는지 봅니다. */
        class CountingRleCodec final : public ICompressionCodec
        {
        public:
            explicit CountingRleCodec( uint32* pDecompressCount )
                : _rleCodec{}
                , _pDecompressCount{ pDecompressCount }
            {
            }

            CompressionCodecType getCodecType() const override { return CompressionCodecType::RLE; }
            const utf8*          getCodecName() const override { return "CountingRle"; }
            size_t               compressBound( size_t uncompressedSize ) const override { return _rleCodec.compressBound( uncompressedSize ); }
            size_t               decompressBound( size_t compressedSize ) const override { return _rleCodec.decompressBound( compressedSize ); }

            bool compress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity, size_t& outCompressedSize, int32 compressionLevel ) override
            {
                return _rleCodec.compress( pSrc, srcSize, pDst, dstCapacity, outCompressedSize, compressionLevel );
            }

            bool decompress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity, size_t& outUncompressedSize ) override
            {
                ++( *_pDecompressCount );
                return _rleCodec.decompress( pSrc, srcSize, pDst, dstCapacity, outUncompressedSize );
            }

        private:
            RleCompressionCodec _rleCodec;
            uint32*             _pDecompressCount;
        };
    } // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// Test 1: 기본 포맷 및 단일 팩 O(1) 해시 읽기 & 무결성 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( ResourcePakTest, SinglePackMountAndHashLookup )
{
    const sw::string testPackPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_temp_pack_01.pack" );

    const sw::vector<sw::pair<sw::string, sw::string>> listFile = {
        {"maps/title.scene.xml",         "<Scene name=\"Title\" version=\"1.0\"/>"},
        {    "shaders/pbr.hlsl",             "// PBR Forward Lighting Shader Code"},
        {     "data/items.json", "{\"sword\": {\"atk\": 50, \"durability\": 100}}"}
    };

    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( testPackPath, 0, sw::PackCompressionType::RLE, listFile ) );

    sw::ResourcePakReader reader;
    SW_ASSERT_TRUE( reader.open( testPackPath ) );
    SW_EXPECT_TRUE( reader.isOpen() );
    SW_EXPECT_EQUAL( reader.getFileCount(), 3u );
    SW_EXPECT_EQUAL( reader.getDlcAppID(), 0u );

    // O(1) 해시 존재 확인
    SW_EXPECT_TRUE( reader.hasFile( "maps/title.scene.xml" ) );
    SW_EXPECT_TRUE( reader.hasFile( "MAPS/TITLE.SCENE.XML" ) ); // 대소문자 무시 해시
    SW_EXPECT_TRUE( reader.hasFile( "shaders/pbr.hlsl" ) );
    SW_EXPECT_TRUE( reader.hasFile( "data/items.json" ) );
    SW_EXPECT_FALSE( reader.hasFile( "non_existent.txt" ) );

    // 텍스트 본문 검증
    sw::string textContent;
    SW_ASSERT_TRUE( reader.readTextFile( "maps/title.scene.xml", textContent ) );
    SW_EXPECT_EQUAL( textContent, "<Scene name=\"Title\" version=\"1.0\"/>" );

    SW_ASSERT_TRUE( reader.readTextFile( "data/items.json", textContent ) );
    SW_EXPECT_EQUAL( textContent, "{\"sword\": {\"atk\": 50, \"durability\": 100}}" );

    reader.close();
}

// ------------------------------------------------------------------------------
// Test 2: VFS 우선순위 오버라이드 스택 검증 (patch > patch_dlc > dlc > game > common > engine)
// ------------------------------------------------------------------------------
/**
 * @brief [ResourcePakTest] 팩이 아는 압축 종류마다 왕복되는가 — 리플렉션된 `PackCompressionType` 의 모든 값(Custom 제외).
 * @details 팩은 자기 디스크 enum(`PackCompressionType`)을 표 한 곳(`ResourcePakCompressionUtil::kArrCodecMapping`)에서 스트림 쪽
 *          `CompressionCodecType` 으로 옮기고 레지스트리에서 코덱을 찾는다. 팩 종류를 늘리고 표에 줄을 빠뜨리면 그 종류에서 여기서 진다.
 */
SW_TEST_CASE( ResourcePakTest, EveryPackCodecRoundTrips )
{
    const sw::vector<sw::pair<sw::string, sw::string>> listFile = {
        { "maps/title.scene.xml", "<Scene name=\"Title\" version=\"1.0\"/>" },
        { "data/items.json", "{\"sword\": {\"atk\": 50, \"durability\": 100}}" },
        { "text/long.txt", sw::string( 4096, 'A' ) },
    };

    const sw::EnumInfo* pPackCompressionEnum = sw::engine::getSubsystem<sw::TypeRegistry>().findEnum<sw::PackCompressionType>();
    SW_ASSERT_NOT_NULL( pPackCompressionEnum );
    uint32 testedCount{ 0 };
    for ( const auto& [value, name] : pPackCompressionEnum->_mapValueToName )
    {
        const sw::PackCompressionType type = static_cast<sw::PackCompressionType>( value );
        if ( type == sw::PackCompressionType::Custom )
            continue;
        ++testedCount;

        sw::CompressionCodecType codecType{};
        SW_EXPECT_TRUE_MSG( sw::ResourcePakCompressionUtil::findCodecType( type, codecType ), name.c_str() );
        const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), sw::string( "test_codec_" ) + name.c_str() + ".pack" );

        SW_EXPECT_TRUE_MSG( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, type, listFile ), name.c_str() );

        sw::ResourcePakReader reader;
        SW_EXPECT_TRUE_MSG( reader.open( packPath ), name.c_str() );
        if ( reader.isOpen() == false )
            continue;
        SW_EXPECT_EQUAL( reader.getFileCount(), static_cast<uint32>( listFile.size() ) );

        for ( const auto& [relPath, expected] : listFile )
        {
            sw::vector<uint8> bytes;
            SW_EXPECT_TRUE_MSG( reader.readFile( relPath, bytes ), name.c_str() );
            const sw::string restored( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
            SW_EXPECT_EQUAL( expected, restored );
        }

        reader.close();
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( packPath ) );
    }
    // 표의 줄 수 = 엔진이 아는 팩 종류 수(Custom 제외).
    SW_EXPECT_EQUAL( static_cast<uint32>( sizeof( sw::ResourcePakCompressionUtil::kArrCodecMapping ) / sizeof( sw::ResourcePakCompressionUtil::kArrCodecMapping[0] ) ), testedCount );
}

/**
 * @brief [ResourcePakTest] 팩 리더는 코덱을 활성 레지스트리에서 찾는다 — 레지스트리가 바꾼 구현을 팩도 쓴다
 * @details 리더가 코덱 클래스를 직접 만들면 레지스트리에서 같은 종류를 바꿔도(엔진 · 모듈의 다른 구현) 팩만 옛 구현을 쓴다. 여기서는 RLE 자리를
 *          해제 횟수를 세는 코덱으로 바꾸고 RLE 팩을 읽는다. 끝나면 내장 RLE 로 되돌린다.
 */
SW_TEST_CASE( ResourcePakTest, PackReaderUsesTheActiveCodecRegistry )
{
    sw::CompressionCodecRegistry* pRegistry = sw::CompressionCodecRegistry::getActive();
    SW_ASSERT_NOT_NULL( pRegistry );

    uint32 decompressCount{ 0 };
    pRegistry->registerCodec( sw::make_unique<sw::CountingRleCodec>( &decompressCount ) );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [pRegistry]()
    {
        pRegistry->registerCodec( sw::make_unique<sw::RleCompressionCodec>() );
    } ) );

    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_codec_registry.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::RLE, {
                                                                                                                  { "text/long.txt", sw::string( 4096, 'B' ) }
    } ) );

    sw::ResourcePakReader reader;
    SW_ASSERT_TRUE( reader.open( packPath ) );
    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( reader.readFile( "text/long.txt", bytes ) );
    SW_EXPECT_EQUAL( size_t( 4096 ), bytes.size() );
    reader.close();

    SW_EXPECT_TRUE_MSG( decompressCount >= 1u, "팩 리더가 등록부의 RLE 코덱을 지나가지 않았다" );
}

SW_TEST_CASE( ResourcePakTest, VFSPriorityStackAndOverrides )
{
    const sw::GlobalVfsScope vfsScope;

    const sw::string enginePack = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_vfs_engine.pack" );
    const sw::string gamePack   = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_vfs_game_main.pack" );
    const sw::string dlcPack    = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_vfs_dlc_exp1.pack" );
    const sw::string patchPack  = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_vfs_patch.pack" );

    // 1. 각 팩에 동일한 키의 파일 생성
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( enginePack, 0, sw::PackCompressionType::None, {
                                                                                                                     { "config/gameplay.xml", "VERSION_ENGINE" }
    } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( gamePack, 0, sw::PackCompressionType::None, {
                                                                                                                   { "config/gameplay.xml", "VERSION_GAME" }
    } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( dlcPack, 0, sw::PackCompressionType::None, {
                                                                                                                  { "config/gameplay.xml", "VERSION_DLC" }
    } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( patchPack, 0, sw::PackCompressionType::None, {
                                                                                                                    { "config/gameplay.xml", "VERSION_PATCH_HOTFIX" }
    } ) );

    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::ResourcePakManager& packManager = sw::ResourceIO::getPackManager();
    packManager.unmountAll();

    // 2. 엔진 & 게임 팩 마운트
    SW_ASSERT_TRUE( packManager.mountPack( enginePack, 3000 ) );
    SW_ASSERT_TRUE( packManager.mountPack( gamePack, 5000 ) );

    sw::string content;
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "config/gameplay.xml", content ) );
    SW_EXPECT_EQUAL( content, "VERSION_GAME" ); // game(5000) > engine(3000)

    // 3. DLC 팩 마운트 (DLC가 본편 오버라이드)
    SW_ASSERT_TRUE( packManager.mountPack( dlcPack, 8000 ) );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "config/gameplay.xml", content ) );
    SW_EXPECT_EQUAL( content, "VERSION_DLC" ); // dlc(8000) > game(5000)

    // 4. 긴급 핫픽스 팩 마운트 (patch.pack이 최우선 오버라이드)
    SW_ASSERT_TRUE( packManager.mountPack( patchPack, 10000 ) );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "config/gameplay.xml", content ) );
    SW_EXPECT_EQUAL( content, "VERSION_PATCH_HOTFIX" ); // patch(10000) > dlc(8000)

    // 5. 핫픽스 언마운트 시 DLC로 안전하게 롤백
    packManager.unmountPack( patchPack );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "config/gameplay.xml", content ) );
    SW_EXPECT_EQUAL( content, "VERSION_DLC" );

    packManager.unmountAll();
}

// ------------------------------------------------------------------------------
// Test 3: 유료 DLC 소유권 검증 (Entitlement Check) 보안 테스트
// ------------------------------------------------------------------------------
SW_TEST_CASE( ResourcePakTest, DlcEntitlementProtection )
{
    const sw::string dlcPackPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_secure_dlc.pack" );
    constexpr uint32 kDlcAppID   = 5001;

    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( dlcPackPath, kDlcAppID, sw::PackCompressionType::None, {
                                                                                                                              { "dlc/secret_weapon.xml", "<Weapon name=\"Excalibur\"/>" }
    } ) );

    sw::ResourcePakManager packManager;

    // 소유권 검증 콜백 등록: 5001번 미소유 상태
    packManager.setDlcEntitlementValidator( []( uint32 appID ) -> bool
    {
        return appID == 9999; // 5001은 미소유 (false)
    } );

    // 미소유 시 마운트 거부 확인
    SW_EXPECT_FALSE( packManager.mountPack( dlcPackPath, 8000 ) );
    SW_EXPECT_FALSE( packManager.hasFile( "dlc/secret_weapon.xml" ) );

    // 유저가 DLC 5001을 정상 구매한 상태로 콜백 변경
    packManager.setDlcEntitlementValidator( []( uint32 appID ) -> bool
    {
        return appID == kDlcAppID; // 5001 소유 확인 (true)
    } );

    // 정상 소유 시 마운트 허용 및 로드 성공 확인
    SW_ASSERT_TRUE( packManager.mountPack( dlcPackPath, 8000 ) );
    SW_EXPECT_TRUE( packManager.hasFile( "dlc/secret_weapon.xml" ) );

    sw::string text;
    SW_ASSERT_TRUE( packManager.readTextFile( "dlc/secret_weapon.xml", text ) );
    SW_EXPECT_EQUAL( text, "<Weapon name=\"Excalibur\"/>" );

    packManager.unmountAll();
}

// ------------------------------------------------------------------------------
// Test 4: 낱개 파일(Loose File) 우선 로드 옵션 (bAllowLooseFiles) 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( ResourcePakTest, LooseFileOverrideOption )
{
    const sw::GlobalVfsScope vfsScope;

    const sw::string packPath  = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_loose_opt.pack" );
    const sw::string loosePath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_loose_file.xml" );

    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                                                                   { "test_loose_file.xml", "CONTENT_IN_PACK" }
    } ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( loosePath, "CONTENT_ON_DISK" ) );

    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::ResourcePakManager& packManager = sw::ResourceIO::getPackManager();
    packManager.unmountAll();
    SW_ASSERT_TRUE( packManager.mountPack( packPath, 5000 ) );

    // 1. 기본 상태: 팩 내용이 우선
    packManager.setAllowLooseFiles( false );
    sw::string content;
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "test_loose_file.xml", content ) );
    SW_EXPECT_EQUAL( content, "CONTENT_IN_PACK" );

    // 2. 모딩/개발용 Loose File 우선 모드 활성화: 디스크의 낱개 파일이 우선
    packManager.setAllowLooseFiles( true );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( loosePath, content ) );
    SW_EXPECT_EQUAL( content, "CONTENT_ON_DISK" );

    packManager.setAllowLooseFiles( true );
    packManager.unmountAll();
}

/**
 * @brief [ResourcePakTest] 텍스트 읽기와 바이너리 읽기가 **같은 것을 고른다**
 * @details `readTextResource` 와 `readBinaryResource` 는 찾는 순서가 같아야 한다 —
 *          절대경로 → 낱개 파일 → 팩 → 낱개 폴백. 둘은 한 자리(`readResourceCommon`)를 같이 쓴다 — 순서를
 *          **두 벌로 따로** 적으면 한쪽만 고칠 때 같은 키로 텍스트와 바이너리가 서로 다른 파일을 읽는다.
 * @note 덮는 것은 "둘 중 하나가 어떤 소스를 아예 안 보게 되는" 변이다(팩을 건너뛰게 만들면 깨진다). 낱개 파일과 팩이 경쟁할 때의
 *       우선순위는 `LooseFileWinsOnlyWhenLooseFilesAreAllowed` 가 본다.
 */
SW_TEST_CASE( ResourcePakTest, TextAndBinaryReadsPickTheSameSource )
{
    const sw::GlobalVfsScope vfsScope;

    const sw::string packPath = test::makeTempPath( "same_source.pack" );

    constexpr const utf8* kKey     = "config/same_source.txt";
    constexpr const utf8* kPayload = "PAYLOAD_FROM_PACK";
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                                                                   { kKey, kPayload }
    } ) );

    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::ResourcePakManager& packManager = sw::ResourceIO::getPackManager();
    packManager.unmountAll();
    SW_ASSERT_TRUE( packManager.mountPack( packPath, 5000 ) );

    // 낱개 우선을 켠 상태와 끈 상태 **둘 다** 두 함수가 같은 답을 내야 한다.
    for ( const bool bAllowLoose : { false, true } )
    {
        packManager.setAllowLooseFiles( bAllowLoose );

        sw::string textContent;
        SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( kKey, textContent ) );

        sw::vector<uint8> binaryContent;
        SW_ASSERT_TRUE( sw::ResourceIO::readBinaryResource( kKey, binaryContent ) );

        const sw::string binaryAsText( reinterpret_cast<const utf8*>( binaryContent.data() ), binaryContent.size() );
        SW_EXPECT_TRUE_MSG( textContent == binaryAsText,
                            "같은 키인데 텍스트 읽기와 바이너리 읽기가 다른 내용을 냈습니다" );
        SW_EXPECT_EQUAL( sw::string( kPayload ), textContent );
    }

    // 없는 키는 둘 다 실패해야 한다 — 폴백 단계도 같은 순서라는 뜻이다.
    {
        sw::string        missingText;
        sw::vector<uint8> missingBytes;
        const bool        bTextOk   = sw::ResourceIO::readTextResource( "config/no_such_key.txt", missingText );
        const bool        bBinaryOk = sw::ResourceIO::readBinaryResource( "config/no_such_key.txt", missingBytes );
        SW_EXPECT_TRUE_MSG( bTextOk == bBinaryOk, "없는 키에 대해 두 읽기의 답이 갈렸습니다" );
        SW_EXPECT_TRUE( bTextOk == false );
    }

    packManager.setAllowLooseFiles( true );
    packManager.unmountAll();
}

/**
 * @brief [ResourcePakTest] 같은 키가 팩과 낱개 파일에 다 있으면 — 낱개 허용(Dev · 쿠킹)은 낱개, 팩 전용(배포)은 팩이다. 팩에 없으면 낱개 허용일 때만 읽힌다
 * @details 텍스트 · 바이너리 · 있나 질의가 같은 답을 내야 한다(`readResourceCommon` 한 자리). 리소스 루트를 임시 폴더로 바꿔 그 아래 낱개 파일을 둔다.
 */
SW_TEST_CASE( ResourcePakTest, LooseFileWinsOnlyWhenLooseFilesAreAllowed )
{
    const sw::GlobalVfsScope           vfsScope;
    constexpr const utf8*              kKey = "engine/lookup_order/probe.txt";
    const test::ScopedTempResourceRoot resourceRoot; // GlobalVfsScope 보다 뒤에 두어 먼저 풀리게 한다(검색 우선순위가 원래 루트에서 다시 지어진다)
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( kKey, "FROM_LOOSE" ) );
    const sw::string packPath = test::makeTempPath( "lookup_order.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                                                                   { kKey, "FROM_PACK" }
    } ) );

    sw::ResourcePakManager& packManager = sw::ResourceIO::getPackManager();
    packManager.unmountAll();
    SW_ASSERT_TRUE( packManager.mountPack( packPath, 5000 ) );
    sw::string text;
    sw::string binary;

    packManager.setAllowLooseFiles( true ); // Dev · 쿠킹: 낱개가 이긴다
    SW_ASSERT_TRUE( sw::readTextAndBinaryResource( kKey, text, binary ) );
    SW_EXPECT_EQUAL( sw::string( "FROM_LOOSE" ), text );
    SW_EXPECT_EQUAL( sw::string( "FROM_LOOSE" ), binary );

    packManager.setAllowLooseFiles( false ); // 배포: 팩만
    SW_ASSERT_TRUE( sw::readTextAndBinaryResource( kKey, text, binary ) );
    SW_EXPECT_EQUAL( sw::string( "FROM_PACK" ), text );
    SW_EXPECT_EQUAL( sw::string( "FROM_PACK" ), binary );

    packManager.unmountAll(); // 팩에 없다 — 낱개 허용일 때만
    packManager.setAllowLooseFiles( true );
    SW_EXPECT_TRUE( sw::readTextAndBinaryResource( kKey, text, binary ) );
    SW_EXPECT_TRUE( sw::ResourceIO::hasResource( kKey ) );
    packManager.setAllowLooseFiles( false );
    {
        test::ScopedDefensiveTestLog expected( "a pack-only build refuses a loose read under the resource root" );
        SW_EXPECT_FALSE( sw::readTextAndBinaryResource( kKey, text, binary ) );
    }
    SW_EXPECT_FALSE( sw::ResourceIO::hasResource( kKey ) );
    packManager.setAllowLooseFiles( true );
}

// ------------------------------------------------------------------------------
// Test 5: 동적 우선순위 자동 산출 (Dynamic Priority Auto-Calculation) 고도화 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( ResourcePakTest, DynamicPriorityAutoCalculation )
{
    const sw::GlobalVfsScope vfsScope;

    const sw::string enginePack = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "engine_autotest.pack" );
    const sw::string commonPack = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "common_autotest.pack" );
    const sw::string gamePack   = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "game_main_autotest.pack" );
    const sw::string patchGame  = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "patch_game_main_autotest.pack" );
    const sw::string hotfixPack = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "patch_hotfix_autotest.pack" );

    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( enginePack, 0, sw::PackCompressionType::None, {
                                                                                                                     { "core/version.txt", "ENGINE_1.0" }
    } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( commonPack, 0, sw::PackCompressionType::None, {
                                                                                                                     { "core/version.txt", "COMMON_1.0" }
    } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( gamePack, 0, sw::PackCompressionType::None, {
                                                                                                                   { "core/version.txt", "GAME_1.0" }
    } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( patchGame, 0, sw::PackCompressionType::None, {
                                                                                                                    { "core/version.txt", "PATCH_GAME_1.1" }
    } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( hotfixPack, 0, sw::PackCompressionType::None, {
                                                                                                                     { "core/version.txt", "HOTFIX_GLOBAL_1.2" }
    } ) );

    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::ResourcePakManager& packManager = sw::ResourceIO::getPackManager();
    packManager.unmountAll();
    // 우선순위 토큰 목록 설정: game > common > engine (3개 항목)
    const sw::vector<sw::string> listPriority = { "game", "common", "engine" };
    sw::ResourcePaths::setSearchPriority( listPriority );

    // 1. priority = 0 으로 자동 산출 마운트
    SW_ASSERT_TRUE( packManager.mountPack( enginePack, 0 ) );
    SW_ASSERT_TRUE( packManager.mountPack( commonPack, 0 ) );
    SW_ASSERT_TRUE( packManager.mountPack( gamePack, 0 ) );

    sw::string versionText;
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "GAME_1.0", versionText ); // game(3000) > common(2000) > engine(1000)

    // 2. 게임 전용 패치 팩 마운트
    SW_ASSERT_TRUE( packManager.mountPack( patchGame, 0 ) );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "PATCH_GAME_1.1", versionText ); // patch_game(3500) > game(3000)

    // 3. 글로벌 긴급 핫픽스 팩 마운트
    SW_ASSERT_TRUE( packManager.mountPack( hotfixPack, 0 ) );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "HOTFIX_GLOBAL_1.2", versionText ); // patch_hotfix(4000) > patch_game(3500)

    // 4. 핫픽스 언마운트 시 단계별 롤백 검증
    packManager.unmountPack( hotfixPack );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "PATCH_GAME_1.1", versionText );

    packManager.unmountPack( patchGame );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "GAME_1.0", versionText );

    packManager.unmountPack( gamePack );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "COMMON_1.0", versionText );

    packManager.unmountPack( commonPack );
    SW_ASSERT_TRUE( sw::ResourceIO::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "ENGINE_1.0", versionText );

    packManager.unmountAll();
}

// ------------------------------------------------------------------------------
// Test 6: 무복사 비압축 I/O 및 CRC32 데이터 변조 탐지 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( ResourcePakTest, ZeroCopyAndCrc32CorruptionDetection )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_crc_tamper.pack" );

    const sw::string originalData = "INTEGRITY_CHECK_SAMPLE_PAYLOAD_DATA_1234567890";
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                                                                   { "secure/token.bin", originalData }
    } ) );

    // 1. 정상 상태에서 무복사 바이너리 읽기 및 CRC32 통과
    {
        sw::ResourcePakReader reader;
        SW_ASSERT_TRUE( reader.open( packPath ) );

        sw::vector<uint8> buffer;
        SW_ASSERT_TRUE( reader.readFile( "secure/token.bin", buffer ) );
        SW_EXPECT_EQUAL( buffer.size(), originalData.size() );
        const sw::string readString{ reinterpret_cast<const utf8*>( buffer.data() ), buffer.size() };
        SW_EXPECT_EQUAL( readString, originalData );
        reader.close();
    }

    // 2. 팩 파일의 페이로드 바이트 1개를 의도적으로 변조 (데이터 손상 시뮬레이션)
    {
        FILE* pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
        fopen_s( &pFile, packPath.c_str(), "r+b" );
#else
        pFile = fopen( packPath.c_str(), "r+b" );
#endif
        SW_ASSERT_TRUE( pFile != nullptr );
        // 첫 번째 파일의 페이로드는 섹터 4096에 위치
        fseek( pFile, 4096, SEEK_SET );
        uint8 corruptedByte = 0xFF;
        fwrite( &corruptedByte, 1, 1, pFile );
        fclose( pFile );
    }

    // 3. 변조된 팩 오픈 후 로드 시 CRC32 불일치로 안전하게 거부(false 반환)되는지 검증
    {
        sw::ResourcePakReader reader;
        SW_ASSERT_TRUE( reader.open( packPath ) );

        sw::vector<uint8> buffer;
        // CRC32 불일치 오류 로그와 함께 false를 반환해야 함
        SW_EXPECT_FALSE( reader.readFile( "secure/token.bin", buffer ) );
        SW_EXPECT_TRUE( buffer.empty() );
        reader.close();
    }
}

// ------------------------------------------------------------------------------
// Test 7: 멀티스레드 동시 VFS I/O 안전성 (Concurrent Multi-Threaded Read)
// ------------------------------------------------------------------------------
SW_TEST_CASE( ResourcePakTest, ConcurrentMultiThreadedVfsRead )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_concurrent_vfs.pack" );

    sw::vector<sw::pair<sw::string, sw::string>> listFile;
    for ( uint32 fileIndex = 0; fileIndex < 16; ++fileIndex )
    {
        listFile.push_back( { "data/file_" + sw::to_string( fileIndex ) + ".txt",
                              "CONTENT_PAYLOAD_OF_FILE_" + sw::to_string( fileIndex ) } );
    }

    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::RLE, listFile ) );

    sw::ResourcePakManager manager;
    SW_ASSERT_TRUE( manager.mountPack( packPath, 5000 ) );

    std::atomic<uint32> successCount{ 0 };
    constexpr uint32    kThreadCount    = 8;
    constexpr uint32    kReadsPerThread = 40;

    sw::vector<std::thread> listThread;
    listThread.reserve( kThreadCount );

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&manager, &successCount]()
        {
            for ( uint32 iteration = 0; iteration < kReadsPerThread; ++iteration )
            {
                const uint32     targetFileIndex = ( iteration % 16 );
                const sw::string fileName        = "data/file_" + sw::to_string( targetFileIndex ) + ".txt";
                const sw::string expected        = "CONTENT_PAYLOAD_OF_FILE_" + sw::to_string( targetFileIndex );

                sw::string text;
                if ( manager.readTextFile( fileName, text ) && text == expected )
                    successCount.fetch_add( 1, std::memory_order_relaxed );
            }
        } );
    }

    for ( auto& thread : listThread )
    {
        if ( thread.joinable() )
            thread.join();
    }

    SW_EXPECT_EQUAL( successCount.load(), kThreadCount * kReadsPerThread );

    manager.unmountAll();
}

// ------------------------------------------------------------------------------
// Test 8: 경로 캐시 64비트 정수 해시 룩업 및 무효화 (Path Cache Integrity)
// ------------------------------------------------------------------------------
SW_TEST_CASE( ResourcePakTest, PathCacheZeroAllocationAndInvalidation )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::ResourcePaths::clearPathCache();

    // 1. 존재하지 않는 경로 룩업
    const sw::string nonExistent = sw::ResourcePaths::findLooseFile( "non_existent_folder/file.unknown" ).getDiskPath();
    SW_EXPECT_TRUE( nonExistent.empty() );

    // 2. 엔진 폴더 내 알려진 경로 룩업 및 캐싱 확인
    const sw::string engineFolder = sw::ResourcePaths::findLooseDomainFolder( "engine" ).getDiskPath();
    if ( engineFolder.empty() == false )
    {
        const sw::string pathFirst = sw::ResourcePaths::findLooseFile( "shaders" ).getDiskPath();
        // 캐시 히트 상태에서 100회 반복 룩업 일관성 검증
        for ( uint32 index = 0; index < 100; ++index )
        {
            const sw::string pathRepeat = sw::ResourcePaths::findLooseFile( "shaders" ).getDiskPath();
            SW_EXPECT_EQUAL( pathFirst, pathRepeat );
        }
    }

    // 3. 캐시 초기화 후 재동작 확인
    sw::ResourcePaths::clearPathCache();
}

/**
 * @brief [ResourcePakTest] 도메인 한정 경로 VFS 쿼리 검증 ("test_domain_pack/file.dat")
 */
SW_TEST_CASE( ResourcePakTest, DomainQualifiedQueryInVfs )
{
    const sw::string packPath = test::makeTempPath( "sw_domain_query_pack.pack" );
    // 도메인 이름은 **팩 파일 이름의 줄기**에서 온다. 여기에 이름을 다시 적으면 그것이
    // 두 번째 사본이 되고, 임시 경로가 프로세스마다 달라지는 순간 어긋난다.
    const sw::string fileName = sw::FileUtil::getFileNamePart( packPath );
    const sw::string domain   = fileName.substr( 0, fileName.rfind( '.' ) );

    const sw::vector<sw::pair<sw::string, sw::string>> listFile = {
        { "textures/icon.dat",   "ICON_PAYLOAD_DATA"},
        {"shaders/custom.dat", "SHADER_PAYLOAD_DATA"},
    };

    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, listFile ) );

    sw::ResourcePakManager packManager;
    packManager.setAllowLooseFiles( false ); // 순수 VFS 환경
    SW_ASSERT_TRUE( packManager.mountPack( packPath, 1000 ) );

    // 1. 도메인 없이 상대 경로로 쿼리
    SW_EXPECT_TRUE( packManager.hasFile( "textures/icon.dat" ) );
    sw::string textContent;
    SW_EXPECT_TRUE( packManager.readTextFile( "textures/icon.dat", textContent ) );
    SW_EXPECT_EQUAL( textContent, "ICON_PAYLOAD_DATA" );

    // 2. 도메인 접두사 포함 쿼리 ("<팩 이름 줄기>/textures/icon.dat")
    SW_EXPECT_TRUE( packManager.hasFile( ( domain + "/textures/icon.dat" ).c_str() ) );
    sw::string domainText;
    SW_EXPECT_TRUE( packManager.readTextFile( ( domain + "/textures/icon.dat" ).c_str(), domainText ) );
    SW_EXPECT_EQUAL( domainText, "ICON_PAYLOAD_DATA" );

    // 3. 존재하지 않는 도메인 쿼리 ("wrong_domain/textures/icon.dat")
    SW_EXPECT_FALSE( packManager.hasFile( "wrong_domain/textures/icon.dat" ) );

    // 4. 바이너리 도메인 쿼리 검증
    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( packManager.readFile( ( domain + "/shaders/custom.dat" ).c_str(), bytes ) );
    SW_EXPECT_EQUAL( bytes.size(), strlen( "SHADER_PAYLOAD_DATA" ) );

    packManager.unmountAll();
}

// ------------------------------------------------------------------------------
// 손상된 팩: 헤더가 말하는 구역이 파일 밖일 때
// ------------------------------------------------------------------------------
/**
 * @brief [ResourcePakTest] 헤더의 수를 그대로 믿지 않는다 — 인덱스가 파일 밖이면 거부한다
 * @details 헤더의 `_fileCount` · `_indexOffset` · `_stringPoolSize` 는 **파일에서 온 값**이다.
 *          그대로 `resize` 하면 잘리거나 손상된 팩 하나가 수 기가짜리 할당 요청이 된다.
 *          그리고 헤더는 인덱스 크기를 `_indexSize` 로도 말하므로 리더가 그 값을 대조해야
 *          쿠커와 리더가 레이아웃을 다르게 볼 때 알아챈다.
 */
SW_TEST_CASE( ResourcePakTest, CorruptHeaderGeometryIsRejected )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_corrupt_geometry.pack" );

    const sw::vector<sw::pair<sw::string, sw::string>> listFile = {
        {"data/items.json", "{\"sword\": 1}"},
        { "maps/title.xml",       "<Scene/>"},
    };
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, listFile ) );

    // 멀쩡한 상태에서는 열린다 — 아래 거부가 손상 때문임을 못 박는다.
    {
        sw::ResourcePakReader reader;
        SW_ASSERT_TRUE( reader.open( packPath ) );
        SW_EXPECT_EQUAL( 2u, reader.getFileCount() );
    }

    sw::PackHeader original{};
    SW_ASSERT_TRUE( sw::readPackHeaderFromDisk( packPath, original ) );

    // 1) 파일 수가 터무니없이 크다 — 인덱스 크기도 맞춰 두었으므로 걸러 내는 것은 파일 크기다.
    {
        sw::PackHeader corrupted = original;
        corrupted._fileCount     = 0x0FFFFFFFu;
        corrupted._indexSize     = static_cast<uint64>( corrupted._fileCount ) * sizeof( sw::PackFileEntryOnDisk );
        SW_ASSERT_TRUE( sw::writePackHeaderToDisk( packPath, corrupted ) );

        test::ScopedLogSuppressor suppressor;
        sw::ResourcePakReader     reader;
        SW_EXPECT_FALSE( reader.open( packPath ) );
        SW_EXPECT_FALSE( reader.isOpen() );
    }

    // 2) `_indexSize` 와 `_fileCount` 가 서로 다른 말을 한다.
    {
        sw::PackHeader corrupted = original;
        corrupted._indexSize     = original._indexSize + sizeof( sw::PackFileEntryOnDisk );
        SW_ASSERT_TRUE( sw::writePackHeaderToDisk( packPath, corrupted ) );

        test::ScopedLogSuppressor suppressor;
        sw::ResourcePakReader     reader;
        SW_EXPECT_FALSE( reader.open( packPath ) );
    }

    // 3) 스트링 풀이 파일 끝을 넘어선다.
    {
        sw::PackHeader corrupted  = original;
        corrupted._flags          = original._flags | static_cast<uint16>( sw::PackFlag::HasStringPool );
        corrupted._stringPoolSize = 1ull << 40;
        SW_ASSERT_TRUE( sw::writePackHeaderToDisk( packPath, corrupted ) );

        test::ScopedLogSuppressor suppressor;
        sw::ResourcePakReader     reader;
        SW_EXPECT_FALSE( reader.open( packPath ) );
    }

    // 원래대로 돌려 두면 다시 열린다.
    SW_ASSERT_TRUE( sw::writePackHeaderToDisk( packPath, original ) );
    sw::ResourcePakReader reader;
    SW_EXPECT_TRUE( reader.open( packPath ) );
}

/**
 * @brief [ResourcePakTest] 항목 경로 목록(스트링 풀)이 없거나 손상된 팩은 열리지 않는다
 * @details 폴더 나열(`collectFolderEntries`)은 이 목록으로만 항목 이름을 안다. 목록이 없는 팩을 받아 주면 그 팩의 폴더는 조용히 비어 보인다.
 *          경로는 풀 안의 **NUL 종료 문자열**이다 — 종결자가 풀 안에 없으면 `const utf8*` 를 그대로 넘기는 읽기는 풀 밖까지 훑으므로 거절한다.
 *          경로의 해시가 항목과 다르면 나열이 읽을 수 없는 이름을 내놓으므로 그것도 거절한다.
 */
SW_TEST_CASE( ResourcePakTest, MissingOrCorruptEntryPathListIsRejected )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_entry_path_list.pack" );

    const sw::vector<sw::pair<sw::string, sw::string>> listFile = {
        { "data/items.json", "{\"sword\": 1}" },
    };
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, listFile ) );
    {
        sw::ResourcePakReader reader;
        SW_ASSERT_TRUE( reader.open( packPath ) );
        sw::PackFileEntry entry{};
        SW_ASSERT_TRUE( reader.getFileEntry( "data/items.json", entry ) );
        SW_EXPECT_EQUAL( sw::string( "data/items.json" ), entry._relativePath );
    }

    sw::vector<uint8> original;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( packPath, original ) );
    SW_ASSERT_TRUE( original.empty() == false );
    SW_ASSERT_EQUAL( uint8( 0 ), original.back() ); // 스트링 풀은 파일 맨 끝에 온다

    // 1) 종결자를 글자로 바꿔 경로가 풀 끝을 넘게 한다.
    {
        sw::vector<uint8> bytes = original;
        bytes.back()            = static_cast<uint8>( 'X' );
        SW_ASSERT_TRUE( sw::FileUtil::writeFile( packPath, bytes.data(), static_cast<uint64>( bytes.size() ) ) );
        test::ScopedLogSuppressor suppressor;
        sw::ResourcePakReader     reader;
        SW_EXPECT_FALSE( reader.open( packPath ) );
    }

    // 2) 경로 한 글자를 바꿔 해시와 어긋나게 한다("data/items.json" → "data/itemz.json").
    {
        sw::vector<uint8> bytes = original;
        bytes[bytes.size() - 7] = static_cast<uint8>( 'z' );
        SW_ASSERT_TRUE( sw::FileUtil::writeFile( packPath, bytes.data(), static_cast<uint64>( bytes.size() ) ) );
        test::ScopedLogSuppressor suppressor;
        sw::ResourcePakReader     reader;
        SW_EXPECT_FALSE( reader.open( packPath ) );
    }

    // 3) 헤더에서 경로 목록 플래그를 지운다(옛 쿠커가 Shipping 에서 하던 것).
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( packPath, original.data(), static_cast<uint64>( original.size() ) ) );
    {
        sw::PackHeader header{};
        SW_ASSERT_TRUE( sw::readPackHeaderFromDisk( packPath, header ) );
        header._flags = static_cast<uint16>( header._flags & ~static_cast<uint16>( sw::PackFlag::HasStringPool ) );
        SW_ASSERT_TRUE( sw::writePackHeaderToDisk( packPath, header ) );
        test::ScopedLogSuppressor suppressor;
        sw::ResourcePakReader     reader;
        SW_EXPECT_FALSE( reader.open( packPath ) );
    }
}

/**
 * @brief [ResourcePakTest] 낱개 파일을 허용하지 않고 팩만 마운트해도 `collectResources` 가 폴더 항목을 돌려주고, 그 id 로 읽힌다
 * @details 팩으로 쿠킹한 배포본의 모양이다 — 디스크를 훑는 나열은 여기서 0개가 된다. 전체 경로로 담긴 항목과 도메인 팩(`game_<이름>.pack`)에
 *          도메인을 떼고 담긴 항목이 함께 나오고, 하위 폴더 안 · 확장자가 다른 항목은 빠지며, 결과는 이름순이다.
 */
SW_TEST_CASE( ResourcePakTest, CollectResourcesListsPackOnlyFolder )
{
    const sw::GlobalVfsScope vfsScope;
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const sw::string folder       = "game/listingprobe/data/presets";
    const sw::string fullPathPack = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "listing_fullpath.pack" );
    const sw::string domainPack   = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "game_listingprobe.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( fullPathPack, 0, sw::PackCompressionType::None,
                                                                   { sw::makeListingEntry( folder + "/b.appearance.xml" ), sw::makeListingEntry( folder + "/A.appearance.xml" ),
                                                                     sw::makeListingEntry( folder + "/sub/c.appearance.xml" ), sw::makeListingEntry( folder + "/readme.txt" ),
                                                                     sw::makeListingEntry( "game/listingprobe/data/presets_other/x.appearance.xml" ) } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( domainPack, 0, sw::PackCompressionType::None,
                                                                   { sw::makeListingEntry( "data/presets/d.appearance.xml" ) } ) );

    sw::ResourcePakManager& packManager = sw::ResourceIO::getPackManager();
    packManager.unmountAll();
    packManager.setAllowLooseFiles( false );
    SW_ASSERT_TRUE( packManager.mountPack( fullPathPack, 1000 ) );
    SW_ASSERT_TRUE( packManager.mountPack( domainPack, 1000 ) );

    sw::vector<sw::string> listPath;
    SW_ASSERT_TRUE( sw::ResourceIO::collectResources( folder, ".appearance.xml", listPath ) == sw::ResourceFolderListing::Listed );
    SW_ASSERT_EQUAL( size_t( 3 ), listPath.size() );
    SW_EXPECT_EQUAL( folder + "/A.appearance.xml", listPath[0] ); // 이름순은 대소문자를 가리지 않는다
    SW_EXPECT_EQUAL( folder + "/b.appearance.xml", listPath[1] );
    SW_EXPECT_EQUAL( folder + "/d.appearance.xml", listPath[2] ); // 도메인 팩 항목은 도메인을 다시 붙여 돌려준다
    for ( const sw::string& path : listPath )
    {
        sw::string text;
        SW_ASSERT_TRUE_MSG( sw::ResourceIO::readTextResource( path, text ), path.c_str() );
        SW_EXPECT_TRUE_MSG( sw::StringUtil::endsWith( text, sw::FileUtil::getFileNamePart( path ), true ), text.c_str() ); // 본문은 담을 때의 경로다
    }

    // 거르지 않으면 같은 폴더의 다른 파일도 나오고, 하위 폴더 안은 여전히 빠진다.
    SW_ASSERT_TRUE( sw::ResourceIO::collectResources( folder + "/", {}, listPath ) == sw::ResourceFolderListing::Listed );
    SW_EXPECT_EQUAL( size_t( 4 ), listPath.size() );

    // 같은 입력이면 실행마다 같은 순서다.
    sw::vector<sw::string> listAgain;
    SW_ASSERT_TRUE( sw::ResourceIO::collectResources( folder, {}, listAgain ) == sw::ResourceFolderListing::Listed );
    SW_EXPECT_TRUE( listAgain == listPath );
}

/**
 * @brief [ResourcePakTest] 낱개 파일을 허용하면 디스크 폴더와 팩 항목의 합집합을 돌려준다. 같은 이름(대소문자 무시)은 하나만 남고 디스크 표기가 이긴다
 */
SW_TEST_CASE( ResourcePakTest, CollectResourcesUnionsLooseFolderAndPacks )
{
    const sw::GlobalVfsScope vfsScope;
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    // 디스크에 실제로 있는 폴더(슈터 외형 데이터)를 쓴다 — 여섯 파일이 있다.
    const sw::string        folder      = "game/shooter3d/data/appearance";
    sw::ResourcePakManager& packManager = sw::ResourceIO::getPackManager();
    packManager.setAllowLooseFiles( true );

    sw::vector<sw::string> listLoose;
    SW_ASSERT_TRUE( sw::ResourceIO::collectResources( folder, ".xml", listLoose ) == sw::ResourceFolderListing::Listed );
    SW_ASSERT_TRUE( listLoose.empty() == false );

    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "listing_union.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None,
                                                                   { sw::makeListingEntry( folder + "/zz_packonly.xml" ), sw::makeListingEntry( folder + "/SLOTS.xml" ) } ) );
    SW_ASSERT_TRUE( packManager.mountPack( packPath, 1000 ) );

    sw::vector<sw::string> listUnion;
    SW_ASSERT_TRUE( sw::ResourceIO::collectResources( folder, ".xml", listUnion ) == sw::ResourceFolderListing::Listed );
    SW_ASSERT_EQUAL( listLoose.size() + 1, listUnion.size() ); // SLOTS.xml 은 디스크의 slots.xml 과 같은 항목이다
    SW_EXPECT_EQUAL( folder + "/zz_packonly.xml", listUnion.back() );
    const sw::string packSpelling  = folder + "/SLOTS.xml";
    const sw::string diskSpelling  = folder + "/slots.xml";
    bool             bDiskSpelling = false;
    for ( const sw::string& path : listUnion )
    {
        SW_EXPECT_TRUE_MSG( sw::StringUtil::equals( path, packSpelling, false ) == false, "디스크 표기(slots.xml)가 남아야 한다" );
        bDiskSpelling = bDiskSpelling || sw::StringUtil::equals( path, diskSpelling, false );
    }
    SW_EXPECT_TRUE( bDiskSpelling );
    for ( size_t index = 1; index < listUnion.size(); ++index )
    {
        SW_EXPECT_TRUE( sw::StringUtil::toLower( listUnion[index - 1].c_str() ) < sw::StringUtil::toLower( listUnion[index].c_str() ) );
    }
}

/**
 * @brief [ResourcePakTest] 폴더가 어디에도 없으면 `FolderMissing`, 폴더 인자가 리소스 id 가 아니면(OS 절대 경로 · `..`) `Failed` 다 — 둘을 섞지 않는다
 */
SW_TEST_CASE( ResourcePakTest, CollectResourcesTellsMissingFolderFromFailure )
{
    const sw::GlobalVfsScope vfsScope;
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    sw::vector<sw::string> listPath{ "stale" };
    SW_EXPECT_TRUE( sw::ResourceIO::collectResources( "game/shooter3d/data/no_such_folder_xyz", ".xml", listPath ) == sw::ResourceFolderListing::FolderMissing );
    SW_EXPECT_TRUE( listPath.empty() );

    test::ScopedLogSuppressor suppressor;
    listPath = { "stale" };
    SW_EXPECT_TRUE( sw::ResourceIO::collectResources( "game/shooter3d/../../escape", ".xml", listPath ) == sw::ResourceFolderListing::Failed );
    SW_EXPECT_TRUE( listPath.empty() );
    SW_EXPECT_TRUE( sw::ResourceIO::collectResources( sw::ResourcePaths::getRootFolderPath(), {}, listPath ) == sw::ResourceFolderListing::Failed );
    SW_EXPECT_TRUE( sw::ResourceIO::collectResources( "", {}, listPath ) == sw::ResourceFolderListing::Failed );
}

// 디스크 전용 해석 결과는 문자열로 저절로 바뀌지 않는다 — 바뀌면 `FileUtil` 로 그대로 흘러가 팩 빌드에서 조용히 "없음" 이 된다.
static_assert( std::is_convertible_v<sw::LooseFilePath, sw::string> == false );
static_assert( std::is_convertible_v<sw::LooseFilePath, sw::string_view> == false );
// `sw::string` 은 제약 없는 explicit 템플릿 생성자(StringViewLike)가 있어 is_constructible 로는 볼 수 없다 — 암시 변환 금지 둘과 string_view 로 본다.
static_assert( std::is_constructible_v<sw::string_view, sw::LooseFilePath> == false );
static_assert( std::is_constructible_v<sw::LooseFilePath, sw::string> == false ); // 만드는 쪽은 ResourcePaths 하나다

/**
 * @brief [ResourcePakTest] FAT 항목이 적어 둔 크기도 파일로 검증되는지 확인
 * @details `validateHeaderGeometry` 는 **헤더의 구역**(인덱스 · 스트링 풀)뿐 아니라 **항목**도 측정한다.
 *          `readFile` 은 항목이 적어 둔 크기를 그대로 `resize` 에 넣으므로 — `_uncompressedSize` 는
 *          `uint32` 라 — 측정하지 않으면 손상된 32바이트 항목 하나가 **4GB 할당 요청**이 된다.
 *
 *          항목을 한 번 걸러 두면 `readFile` 은 그 값을 믿어도 된다 — 그래서 검사는 여는
 *          시점에 있고, 이 케이스도 `open()` 이 거부하는지를 본다.
 */
SW_TEST_CASE( ResourcePakTest, CorruptEntrySizeIsRejected )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_corrupt_entry.pack" );

    const sw::vector<sw::pair<sw::string, sw::string>> listFile = {
        {"data/items.json", "{\"sword\": 1}"},
        { "maps/title.xml",       "<Scene/>"},
    };
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, listFile ) );

    // 멀쩡한 상태에서는 열리고 읽힌다 — 아래 거부가 손상 때문임을 못 박는다.
    {
        sw::ResourcePakReader reader;
        SW_ASSERT_TRUE( reader.open( packPath ) );
        sw::vector<uint8> bytes;
        SW_EXPECT_TRUE( reader.readFile( sw::string_view{ "data/items.json" }, bytes ) );
    }

    sw::PackFileEntryOnDisk original{};
    SW_ASSERT_TRUE( sw::readPackEntryFromDisk( packPath, 0, original ) );

    // 1) 항목이 파일 끝을 한참 넘는 크기를 적어 두었다.
    {
        sw::PackFileEntryOnDisk corrupted = original;
        corrupted._compressedSize         = 0xFFFFFFFFu;
        corrupted._uncompressedSize       = 0xFFFFFFFFu;
        SW_ASSERT_TRUE( sw::writePackEntryToDisk( packPath, 0, corrupted ) );

        test::ScopedLogSuppressor suppressor;
        sw::ResourcePakReader     reader;
        SW_EXPECT_FALSE( reader.open( packPath ) );
        SW_EXPECT_FALSE( reader.isOpen() );
    }

    // 2) 오프셋이 파일 밖을 가리킨다.
    {
        sw::PackFileEntryOnDisk corrupted = original;
        corrupted._dataOffset             = 1ull << 40;
        SW_ASSERT_TRUE( sw::writePackEntryToDisk( packPath, 0, corrupted ) );

        test::ScopedLogSuppressor suppressor;
        sw::ResourcePakReader     reader;
        SW_EXPECT_FALSE( reader.open( packPath ) );
    }

    // 원래대로 돌려 두면 다시 열린다.
    SW_ASSERT_TRUE( sw::writePackEntryToDisk( packPath, 0, original ) );
    sw::ResourcePakReader reader;
    SW_EXPECT_TRUE( reader.open( packPath ) );
}

/**
 * @brief [ResourcePakTest] 리더를 옮기면(이동 생성 · 이동 대입) 연 팩이 통째로 따라가고, 원래 리더는 닫힌다
 * @details 엔진은 리더를 `unique_ptr` 로 들어서 이동 연산을 지나는 코드가 없다. 이동 생성과 이동 대입은 같은 몸통
 *          (`takeFromLocked`)을 쓴다 — 멤버 하나를 더할 때 한 곳만 고친다. 이동 대입은 받는 쪽이 열어 둔 팩을 먼저 닫아야 한다.
 */
SW_TEST_CASE( ResourcePakTest, MovedReaderKeepsTheOpenPack )
{
    const sw::string packPathA = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_temp_pack_move_a.pack" );
    const sw::string packPathB = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_temp_pack_move_b.pack" );

    const sw::vector<sw::pair<sw::string, sw::string>> listFileA = {
        { "data/a.txt", "pack-a" }
    };
    const sw::vector<sw::pair<sw::string, sw::string>> listFileB = {
        {"data/b.txt", "pack-b"},
        {"data/c.txt", "pack-c"}
    };
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPathA, 0, sw::PackCompressionType::None, listFileA ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPathB, 0, sw::PackCompressionType::None, listFileB ) );

    sw::ResourcePakReader source;
    SW_ASSERT_TRUE( source.open( packPathA ) );

    // 이동 생성 — 팩 A 가 옮겨 가고 원래 리더는 닫힌다.
    sw::ResourcePakReader moved( std::move( source ) );
    SW_EXPECT_FALSE( source.isOpen() );
    SW_EXPECT_TRUE( moved.isOpen() );
    SW_EXPECT_EQUAL( 1u, moved.getFileCount() );
    sw::string text;
    SW_ASSERT_TRUE( moved.readTextFile( "data/a.txt", text ) );
    SW_EXPECT_EQUAL( text, "pack-a" );

    // 이동 대입 — 받는 쪽이 열어 둔 팩 B 는 닫히고 팩 A 로 바뀐다.
    sw::ResourcePakReader target;
    SW_ASSERT_TRUE( target.open( packPathB ) );
    target = std::move( moved );
    SW_EXPECT_FALSE( moved.isOpen() );
    SW_EXPECT_TRUE( target.isOpen() );
    SW_EXPECT_EQUAL( 1u, target.getFileCount() );
    SW_EXPECT_TRUE( target.hasFile( "data/a.txt" ) );
    SW_EXPECT_FALSE( target.hasFile( "data/b.txt" ) );
    SW_EXPECT_EQUAL( packPathA, target.getPackPath() );

    target.close();
}

/**
 * @brief [ResourcePakTest] 저장된 CRC 를 0 으로 지워도 검사가 꺼지지 않는다
 * @details 저장된 CRC 가 0 이라고 검사를 건너뛰면 그 칸을 지우고 페이로드를 바꾸는 변조가 통과한다(쿠킹하는 쪽은 모든 항목의 CRC 를
 *          적는다 — 빈 데이터의 CRC 가 0).
 */
SW_TEST_CASE( ResourcePakTest, ZeroedCrcDoesNotDisableTheCheck )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_crc_zeroed.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                                                                   { "secure/zeroed.bin", "PAYLOAD_THAT_WILL_BE_TAMPERED" }
    } ) );

    const uint64 indexOffset = sw::readIndexOffsetInternal( packPath );
    SW_ASSERT_TRUE( indexOffset != 0 );
    const uint32 zeroCrc{ 0 };
    const uint8  tampered{ 0xFF };
    SW_ASSERT_TRUE( sw::patchFileBytesInternal( packPath, indexOffset + 24, &zeroCrc, sizeof( zeroCrc ) ) );
    SW_ASSERT_TRUE( sw::patchFileBytesInternal( packPath, sw::kPackSectorAlignment, &tampered, sizeof( tampered ) ) );

    sw::ResourcePakReader reader;
    SW_ASSERT_TRUE( reader.open( packPath ) );
    sw::vector<uint8> buffer;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( reader.readFile( "secure/zeroed.bin", buffer ) );
    }
    reader.close();
}

/**
 * @brief [ResourcePakTest] 압축 크기 0 인 항목은 0 바이트 N 개가 아니라 실패다
 * @details 원본 크기가 N 인데 압축 크기 0 을 "풀 것 없음" 으로 성공시키면 CRC 가 없는 팩에서 0 으로 찬 데이터를 돌려준다.
 */
SW_TEST_CASE( ResourcePakTest, EmptyCompressedPayloadIsNotSuccess )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_empty_payload.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::RLE, {
                                                                                                                  { "secure/empty.bin", "AAAAAAAAAAAAAAAABBBBBBBBBBBBBBBB" }
    } ) );

    const uint64 indexOffset = sw::readIndexOffsetInternal( packPath );
    SW_ASSERT_TRUE( indexOffset != 0 );
    // CRC 없는 팩으로 만든다 — CRC 가 있으면 0 으로 찬 데이터를 그것이 먼저 잡아, 이 수정만 따로 측정할 수 없다. 항목 경로 목록은 필수라 남긴다.
    const uint32 zero{ 0 };
    const uint16 pathListOnlyFlags = static_cast<uint16>( sw::PackFlag::HasStringPool );
    SW_ASSERT_TRUE( sw::patchFileBytesInternal( packPath, indexOffset + 16, &zero, sizeof( zero ) ) );             // _compressedSize
    SW_ASSERT_TRUE( sw::patchFileBytesInternal( packPath, 16, &pathListOnlyFlags, sizeof( pathListOnlyFlags ) ) ); // PackHeader::_flags

    sw::ResourcePakReader reader;
    SW_ASSERT_TRUE( reader.open( packPath ) );
    sw::vector<uint8> buffer;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( reader.readFile( "secure/empty.bin", buffer ) );
    }
    reader.close();
}

/**
 * @brief [ResourcePakTest] 암호화 표시가 있는 팩은 열지 않는다
 * @details 풀 방법이 없으므로 플래그 · 방식을 보고 거절한다 — 보지 않으면 평문으로 푼다.
 */
SW_TEST_CASE( ResourcePakTest, EncryptedPackIsRefused )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_encrypted_flag.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                                                                   { "secure/enc.bin", "PLAINTEXT" }
    } ) );

    const uint16 flags = static_cast<uint16>( static_cast<uint16>( sw::PackFlag::HasCrc32 ) | static_cast<uint16>( sw::PackFlag::Encrypted ) );
    SW_ASSERT_TRUE( sw::patchFileBytesInternal( packPath, 16, &flags, sizeof( flags ) ) ); // PackHeader::_flags

    sw::ResourcePakReader reader;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( reader.open( packPath ) );
    }
    reader.close();
}

/**
 * @brief [ResourcePakTest] 비동기 읽기는 동기 읽기와 같은 바이트를 주고(압축 해제 · CRC 포함), 걸어 둔 뒤 팩을 내려도 끝까지 읽는다
 * @details 완료는 리더가 아니라 파일 핸들 사본과 항목 정보를 든다. 언마운트가 리더를 지워도 진행 중인 읽기는 성공해야 한다.
 *          어느 팩에도 없는 경로는 아무것도 걸지 않는다(무효 핸들, 콜백 없음).
 */
SW_TEST_CASE( ResourcePakTest, AsyncReadMatchesSyncReadAndSurvivesUnmount )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_async_read.pack" );

    constexpr uint32                             kEntryCount = 32;
    sw::vector<sw::pair<sw::string, sw::string>> listFile;
    for ( uint32 index = 0; index < kEntryCount; ++index )
    {
        listFile.push_back( { "async/entry_" + sw::to_string( index ) + ".bin", sw::string( 100 + index * 997, static_cast<utf8>( 'a' + ( index % 26 ) ) ) } );
    }
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::LZ4, listFile ) );

    sw::ResourcePakManager manager;
    SW_ASSERT_TRUE( manager.mountPack( packPath, 5000 ) );

    sw::AsyncFileIO&                io = sw::engine::getSubsystem<sw::AsyncFileIO>();
    sw::vector<sw::vector<uint8>>   listBytes( kEntryCount );
    sw::vector<uint8>               listOk( kEntryCount, 0 );
    sw::vector<sw::AsyncReadHandle> listHandle;
    std::atomic<uint32>             callbackCount{ 0 };
    for ( uint32 index = 0; index < kEntryCount; ++index )
    {
        sw::vector<uint8>* pBytes = &listBytes[index];
        uint8*             pOk    = &listOk[index];
        listHandle.push_back( manager.readFileAsync( io, listFile[index].first, sw::AsyncIOPriority::Normal,
                                                     SW_DELEGATE_LAMBDA( sw::ResourceReadCompleteDelegate, [pBytes, pOk, &callbackCount]( bool bSuccess, sw::vector<uint8>& bytes )
        {
            *pOk = bSuccess ? 1 : 0;
            pBytes->swap( bytes );
            callbackCount.fetch_add( 1 );
        } ) ) );
        SW_EXPECT_TRUE( listHandle.back().isValid() );
    }

    // 걸어 둔 채로 내린다 — 리더는 사라져도 읽기는 끝나야 한다.
    manager.unmountAll();
    for ( const sw::AsyncReadHandle& handle : listHandle )
    {
        SW_ASSERT_TRUE( handle.waitFor( 10000 ) );
    }

    SW_EXPECT_EQUAL( callbackCount.load(), kEntryCount );
    for ( uint32 index = 0; index < kEntryCount; ++index )
    {
        SW_EXPECT_EQUAL( listOk[index], uint8{ 1 } );
        const sw::string text{ reinterpret_cast<const utf8*>( listBytes[index].data() ), listBytes[index].size() };
        SW_EXPECT_TRUE( text == listFile[index].second );
    }

    // 없는 경로는 아무것도 걸지 않는다.
    SW_ASSERT_TRUE( manager.mountPack( packPath, 5000 ) );
    bool                      bMissingCalled = false;
    const sw::AsyncReadHandle missing        = manager.readFileAsync( io, "async/not_there.bin", sw::AsyncIOPriority::Normal,
                                                                      SW_DELEGATE_LAMBDA( sw::ResourceReadCompleteDelegate, [&bMissingCalled]( bool, sw::vector<uint8>& )
           {
        bMissingCalled = true;
    } ) );
    SW_EXPECT_FALSE( missing.isValid() );
    SW_EXPECT_FALSE( bMissingCalled );
    manager.unmountAll();
}

/**
 * @brief [ResourcePakTest] 비동기 읽기도 CRC 가 어긋난 항목을 실패로 돌려준다(바이트 없음)
 */
SW_TEST_CASE( ResourcePakTest, AsyncReadRejectsCrcMismatch )
{
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "test_async_crc.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                                                                   { "secure/async.bin", "ASYNC_INTEGRITY_PAYLOAD" }
    } ) );
    const uint8 corruptedByte = 0xFF;
    SW_ASSERT_TRUE( sw::patchFileBytesInternal( packPath, 4096, &corruptedByte, 1 ) ); // 첫 페이로드는 섹터 4096

    sw::ResourcePakReader reader;
    SW_ASSERT_TRUE( reader.open( packPath ) );
    const sw::string  entryPath = "secure/async.bin";
    bool              bCalled   = false;
    bool              bSuccess  = true;
    sw::vector<uint8> received;
    {
        test::ScopedLogSuppressor suppressor;
        const sw::AsyncReadHandle handle = reader.readFileAsync( sw::engine::getSubsystem<sw::AsyncFileIO>(), sw::StringUtil::computeHash64( sw::string_view{ entryPath } ), sw::AsyncIOPriority::High,
                                                                 SW_DELEGATE_LAMBDA( sw::ResourceReadCompleteDelegate, [&]( bool bRead, sw::vector<uint8>& bytes )
        {
            bCalled  = true;
            bSuccess = bRead;
            received.swap( bytes );
        } ) );
        SW_ASSERT_TRUE( handle.isValid() );
        SW_ASSERT_TRUE( handle.waitFor( 10000 ) );
    }
    SW_EXPECT_TRUE( bCalled );
    SW_EXPECT_FALSE( bSuccess );
    SW_EXPECT_TRUE( received.empty() );
    reader.close();
}
