#include "pch.h"

#include "Core/Compression/RleCompressionCodec.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourcePackManager.h"
#include "Engine/Resource/ResourcePackReader.h"
#include "Engine/Resource/ResourcePackTypes.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

#include <atomic>
#include <functional>
#include <thread>

namespace sw
{
    namespace
    {
        /**
         * @brief 테스트용 팩 파일을 생성하는 헬퍼 함수
         */
        bool createTestPackFile( const string& packPath, uint32 dlcAppId, PackCompressionType compression, const vector<std::pair<string, string>>& listFileContent, bool bIncludeDebugStringPool = false )
        {
            FileUtil::createParentDirectory( packPath );

            FILE* pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
            fopen_s( &pFile, packPath.c_str(), "wb" );
#else
            pFile = fopen( packPath.c_str(), "wb" );
#endif
            if ( pFile == nullptr )
                return false;

            PackHeader header{};
            header._magic           = kPackMagic;
            header._formatVersion   = kPackFormatVersion;
            header._dlcAppId        = dlcAppId;
            header._compressionType = static_cast<uint8>( compression );
            header._encryptionType  = static_cast<uint8>( PackEncryptionType::None );
            header._sectorAlignment = kPackSectorAlignment;
            header._flags           = static_cast<uint16>( PackFlag::HasCrc32 );
            if ( bIncludeDebugStringPool )
                header._flags |= static_cast<uint16>( PackFlag::HasStringPool );
            header._fileCount = static_cast<uint32>( listFileContent.size() );

            // 페이로드 임시 버퍼 준비
            vector<uint8>               dataStreamBytes;
            vector<PackFileEntryOnDisk> listDiskEntry;
            vector<utf8>                stringPoolBytes;

            uint64 curOffset = kPackSectorAlignment; // 헤더 이후 첫 데이터 블록은 4096에서 시작

            for ( const auto& [relPath, content] : listFileContent )
            {
                const uint64 pathHash   = StringUtil::computeHash64( relPath );
                const uint32 uncompSize = static_cast<uint32>( content.size() );
                const uint32 crc        = StringUtil::computeCrc32( content.data(), uncompSize );

                vector<uint8> compressedPayloadBytes;
                if ( compression == PackCompressionType::RLE && uncompSize > 0 )
                {
                    RleCompressionCodec codec;
                    const size_t        bound = codec.compressBound( uncompSize );
                    compressedPayloadBytes.resize( bound );
                    size_t compSize = 0;
                    codec.compress( content.data(), uncompSize, compressedPayloadBytes.data(), bound, compSize );
                    compressedPayloadBytes.resize( compSize );
                }
                else
                {
                    const auto* pContentBytes = reinterpret_cast<const uint8*>( content.data() );
                    compressedPayloadBytes.assign( pContentBytes, pContentBytes + content.size() );
                }

                const uint32 compSize      = static_cast<uint32>( compressedPayloadBytes.size() );
                const uint64 alignedOffset = MathUtil::align( curOffset, static_cast<uint64>( kPackSectorAlignment ) );

                // 패딩 추가
                const size_t padding = static_cast<size_t>( alignedOffset - curOffset );
                if ( padding > 0 )
                {
                    dataStreamBytes.insert( dataStreamBytes.end(), padding, 0 );
                }

                dataStreamBytes.insert( dataStreamBytes.end(), compressedPayloadBytes.begin(), compressedPayloadBytes.end() );
                curOffset = alignedOffset + compSize;

                PackFileEntryOnDisk diskEntry{};
                diskEntry._pathHash         = pathHash;
                diskEntry._dataOffset       = alignedOffset;
                diskEntry._compressedSize   = compSize;
                diskEntry._uncompressedSize = uncompSize;
                diskEntry._crc32            = crc;

                if ( bIncludeDebugStringPool )
                {
                    diskEntry._stringPoolOffset = static_cast<uint32>( stringPoolBytes.size() );
                    stringPoolBytes.insert( stringPoolBytes.end(), relPath.begin(), relPath.end() );
                    stringPoolBytes.push_back( '\0' );
                }

                listDiskEntry.push_back( diskEntry );
            }

            header._totalDataSize = dataStreamBytes.size();
            header._indexOffset   = MathUtil::align( static_cast<uint64>( kPackSectorAlignment ) + header._totalDataSize, uint64{ 64 } );
            header._indexSize     = listDiskEntry.size() * sizeof( PackFileEntryOnDisk );

            header._stringPoolOffset = header._indexOffset + header._indexSize;
            header._stringPoolSize   = stringPoolBytes.size();

            // 1. 헤더(64B) 기록
            std::fwrite( &header, 1, sizeof( PackHeader ), pFile );

            // 2. 4096 섹터 경계까지 패딩
            const size_t headerPadding = kPackSectorAlignment - sizeof( PackHeader );
            const auto   zeroBuf       = vector<uint8>( headerPadding, 0 );
            std::fwrite( zeroBuf.data(), 1, headerPadding, pFile );

            // 3. 페이로드 기록
            if ( dataStreamBytes.empty() == false )
            {
                std::fwrite( dataStreamBytes.data(), 1, dataStreamBytes.size(), pFile );
            }

            // 4. FAT 인덱스까지 패딩
            const size_t fatPadding = static_cast<size_t>( header._indexOffset - ( kPackSectorAlignment + header._totalDataSize ) );
            if ( fatPadding > 0 )
            {
                const auto fatPadBuf = vector<uint8>( fatPadding, 0 );
                std::fwrite( fatPadBuf.data(), 1, fatPadding, pFile );
            }

            // 5. FAT 인덱스 테이블 기록
            if ( listDiskEntry.empty() == false )
            {
                std::fwrite( listDiskEntry.data(), 1, listDiskEntry.size() * sizeof( PackFileEntryOnDisk ), pFile );
            }

            // 6. 스트링 풀 기록
            if ( bIncludeDebugStringPool && stringPoolBytes.empty() == false )
            {
                std::fwrite( stringPoolBytes.data(), 1, stringPoolBytes.size(), pFile );
            }

            std::fclose( pFile );
            return true;
        }
    } // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// Test 1: 기본 포맷 및 단일 팩 O(1) 해시 읽기 & 무결성 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_ResourcePack, SinglePackMountAndHashLookup )
{
    const sw::string testPackPath = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_temp_pack_01.pack" );

    const sw::vector<std::pair<sw::string, sw::string>> listFile = {
        {"maps/title.scene.xml",         "<Scene name=\"Title\" version=\"1.0\"/>"},
        {    "shaders/pbr.hlsl",             "// PBR Forward Lighting Shader Code"},
        {     "data/items.json", "{\"sword\": {\"atk\": 50, \"durability\": 100}}"}
    };

    SW_ASSERT_TRUE( sw::createTestPackFile( testPackPath, 0, sw::PackCompressionType::RLE, listFile, false ) );

    sw::ResourcePackReader reader;
    SW_ASSERT_TRUE( reader.open( testPackPath ) );
    SW_EXPECT_TRUE( reader.isOpen() );
    SW_EXPECT_EQUAL( reader.getFileCount(), 3u );
    SW_EXPECT_EQUAL( reader.getDlcAppId(), 0u );

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
    sw::FileUtil::removeFile( testPackPath );
}

// ------------------------------------------------------------------------------
// Test 2: VFS 우선순위 오버라이드 스택 검증 (patch > patch_dlc > dlc > game > common > engine)
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_ResourcePack, VFSPriorityStackAndOverrides )
{
    const sw::string enginePack = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_vfs_engine.pack" );
    const sw::string gamePack   = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_vfs_game_main.pack" );
    const sw::string dlcPack    = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_vfs_dlc_exp1.pack" );
    const sw::string patchPack  = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_vfs_patch.pack" );

    // 1. 각 팩에 동일한 키의 파일 생성
    sw::createTestPackFile( enginePack, 0, sw::PackCompressionType::None, {
                                                                              { "config/gameplay.xml", "VERSION_ENGINE" }
    } );
    sw::createTestPackFile( gamePack, 0, sw::PackCompressionType::None, {
                                                                            { "config/gameplay.xml", "VERSION_GAME" }
    } );
    sw::createTestPackFile( dlcPack, 0, sw::PackCompressionType::None, {
                                                                           { "config/gameplay.xml", "VERSION_DLC" }
    } );
    sw::createTestPackFile( patchPack, 0, sw::PackCompressionType::None, {
                                                                             { "config/gameplay.xml", "VERSION_PATCH_HOTFIX" }
    } );

    sw::ResourceUtil::initialize();
    sw::ResourcePackManager& packManager = sw::ResourceUtil::getPackManager();
    packManager.unmountAll();

    // 2. 엔진 & 게임 팩 마운트
    SW_ASSERT_TRUE( packManager.mountPack( enginePack, 3000 ) );
    SW_ASSERT_TRUE( packManager.mountPack( gamePack, 5000 ) );

    sw::string content;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "config/gameplay.xml", content ) );
    SW_EXPECT_EQUAL( content, "VERSION_GAME" ); // game(5000) > engine(3000)

    // 3. DLC 팩 마운트 (DLC가 본편 오버라이드)
    SW_ASSERT_TRUE( packManager.mountPack( dlcPack, 8000 ) );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "config/gameplay.xml", content ) );
    SW_EXPECT_EQUAL( content, "VERSION_DLC" ); // dlc(8000) > game(5000)

    // 4. 긴급 핫픽스 팩 마운트 (patch.pack이 최우선 오버라이드)
    SW_ASSERT_TRUE( packManager.mountPack( patchPack, 10000 ) );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "config/gameplay.xml", content ) );
    SW_EXPECT_EQUAL( content, "VERSION_PATCH_HOTFIX" ); // patch(10000) > dlc(8000)

    // 5. 핫픽스 언마운트 시 DLC로 안전하게 롤백
    packManager.unmountPack( patchPack );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "config/gameplay.xml", content ) );
    SW_EXPECT_EQUAL( content, "VERSION_DLC" );

    packManager.unmountAll();
    sw::FileUtil::removeFile( enginePack );
    sw::FileUtil::removeFile( gamePack );
    sw::FileUtil::removeFile( dlcPack );
    sw::FileUtil::removeFile( patchPack );
}

// ------------------------------------------------------------------------------
// Test 3: 유료 DLC 소유권 검증 (Entitlement Check) 보안 테스트
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_ResourcePack, DlcEntitlementProtection )
{
    const sw::string dlcPackPath = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_secure_dlc.pack" );
    constexpr uint32 kDlcAppId   = 5001;

    SW_ASSERT_TRUE( sw::createTestPackFile( dlcPackPath, kDlcAppId, sw::PackCompressionType::None, {
                                                                                                       { "dlc/secret_weapon.xml", "<Weapon name=\"Excalibur\"/>" }
    } ) );

    sw::ResourcePackManager packManager;

    // 소유권 검증 콜백 등록: 5001번 미소유 상태
    packManager.setDlcEntitlementValidator( []( uint32 appId ) -> bool
    {
        return appId == 9999; // 5001은 미소유 (false)
    } );

    // 미소유 시 마운트 거부 확인
    SW_EXPECT_FALSE( packManager.mountPack( dlcPackPath, 8000 ) );
    SW_EXPECT_FALSE( packManager.hasFile( "dlc/secret_weapon.xml" ) );

    // 유저가 DLC 5001을 정상 구매한 상태로 콜백 변경
    packManager.setDlcEntitlementValidator( []( uint32 appId ) -> bool
    {
        return appId == kDlcAppId; // 5001 소유 확인 (true)
    } );

    // 정상 소유 시 마운트 허용 및 로드 성공 확인
    SW_ASSERT_TRUE( packManager.mountPack( dlcPackPath, 8000 ) );
    SW_EXPECT_TRUE( packManager.hasFile( "dlc/secret_weapon.xml" ) );

    sw::string text;
    SW_ASSERT_TRUE( packManager.readTextFile( "dlc/secret_weapon.xml", text ) );
    SW_EXPECT_EQUAL( text, "<Weapon name=\"Excalibur\"/>" );

    packManager.unmountAll();
    sw::FileUtil::removeFile( dlcPackPath );
}

// ------------------------------------------------------------------------------
// Test 4: 낱개 파일(Loose File) 우선 로드 옵션 (bAllowLooseFiles) 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_ResourcePack, LooseFileOverrideOption )
{
    const sw::string packPath  = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_loose_opt.pack" );
    const sw::string loosePath = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_loose_file.xml" );

    sw::createTestPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                            { "test_loose_file.xml", "CONTENT_IN_PACK" }
    } );
    sw::FileUtil::writeTextFile( loosePath, "CONTENT_ON_DISK" );

    sw::ResourceUtil::initialize();
    sw::ResourcePackManager& packManager = sw::ResourceUtil::getPackManager();
    packManager.unmountAll();
    SW_ASSERT_TRUE( packManager.mountPack( packPath, 5000 ) );

    // 1. 기본 상태: 팩 내용이 우선
    packManager.setAllowLooseFiles( false );
    sw::string content;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "test_loose_file.xml", content ) );
    SW_EXPECT_EQUAL( content, "CONTENT_IN_PACK" );

    // 2. 모딩/개발용 Loose File 우선 모드 활성화: 디스크의 낱개 파일이 우선
    packManager.setAllowLooseFiles( true );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( loosePath, content ) );
    SW_EXPECT_EQUAL( content, "CONTENT_ON_DISK" );

    packManager.setAllowLooseFiles( true );
    packManager.unmountAll();
    sw::FileUtil::removeFile( packPath );
    sw::FileUtil::removeFile( loosePath );
}

// ------------------------------------------------------------------------------
// Test 5: 동적 우선순위 자동 산출 (Dynamic Priority Auto-Calculation) 고도화 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_ResourcePack, DynamicPriorityAutoCalculation )
{
    const sw::string enginePack = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "engine_autotest.pack" );
    const sw::string commonPack = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "common_autotest.pack" );
    const sw::string gamePack   = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "game_main_autotest.pack" );
    const sw::string patchGame  = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "patch_game_main_autotest.pack" );
    const sw::string hotfixPack = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "patch_hotfix_autotest.pack" );

    sw::createTestPackFile( enginePack, 0, sw::PackCompressionType::None, {
                                                                              { "core/version.txt", "ENGINE_1.0" }
    } );
    sw::createTestPackFile( commonPack, 0, sw::PackCompressionType::None, {
                                                                              { "core/version.txt", "COMMON_1.0" }
    } );
    sw::createTestPackFile( gamePack, 0, sw::PackCompressionType::None, {
                                                                            { "core/version.txt", "GAME_1.0" }
    } );
    sw::createTestPackFile( patchGame, 0, sw::PackCompressionType::None, {
                                                                             { "core/version.txt", "PATCH_GAME_1.1" }
    } );
    sw::createTestPackFile( hotfixPack, 0, sw::PackCompressionType::None, {
                                                                              { "core/version.txt", "HOTFIX_GLOBAL_1.2" }
    } );

    sw::ResourceUtil::initialize();
    sw::ResourcePackManager& packManager = sw::ResourceUtil::getPackManager();
    packManager.unmountAll();
    // 우선순위 토큰 목록 설정: game > common > engine (3개 항목)
    const sw::vector<sw::string> listPriority = { "game", "common", "engine" };
    sw::ResourceUtil::setSearchPriority( listPriority );

    // 1. priority = 0 으로 자동 산출 마운트
    SW_ASSERT_TRUE( packManager.mountPack( enginePack, 0 ) );
    SW_ASSERT_TRUE( packManager.mountPack( commonPack, 0 ) );
    SW_ASSERT_TRUE( packManager.mountPack( gamePack, 0 ) );

    sw::string versionText;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "GAME_1.0", versionText ); // game(3000) > common(2000) > engine(1000)

    // 2. 게임 전용 패치 팩 마운트
    SW_ASSERT_TRUE( packManager.mountPack( patchGame, 0 ) );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "PATCH_GAME_1.1", versionText ); // patch_game(3500) > game(3000)

    // 3. 글로벌 긴급 핫픽스 팩 마운트
    SW_ASSERT_TRUE( packManager.mountPack( hotfixPack, 0 ) );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "HOTFIX_GLOBAL_1.2", versionText ); // patch_hotfix(4000) > patch_game(3500)

    // 4. 핫픽스 언마운트 시 단계별 롤백 검증
    packManager.unmountPack( hotfixPack );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "PATCH_GAME_1.1", versionText );

    packManager.unmountPack( patchGame );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "GAME_1.0", versionText );

    packManager.unmountPack( gamePack );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "COMMON_1.0", versionText );

    packManager.unmountPack( commonPack );
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "core/version.txt", versionText ) );
    SW_EXPECT_EQUAL( "ENGINE_1.0", versionText );

    packManager.unmountAll();
    sw::FileUtil::removeFile( enginePack );
    sw::FileUtil::removeFile( commonPack );
    sw::FileUtil::removeFile( gamePack );
    sw::FileUtil::removeFile( patchGame );
    sw::FileUtil::removeFile( hotfixPack );
}

// ------------------------------------------------------------------------------
// Test 6: 무복사 비압축 I/O 및 CRC32 데이터 변조 탐지 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_ResourcePack, ZeroCopyAndCrc32CorruptionDetection )
{
    const sw::string packPath = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_crc_tamper.pack" );

    const sw::string originalData = "INTEGRITY_CHECK_SAMPLE_PAYLOAD_DATA_1234567890";
    SW_ASSERT_TRUE( sw::createTestPackFile( packPath, 0, sw::PackCompressionType::None, {
                                                                                            { "secure/token.bin", originalData }
    } ) );

    // 1. 정상 상태에서 무복사 바이너리 읽기 및 CRC32 통과
    {
        sw::ResourcePackReader reader;
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
        sw::ResourcePackReader reader;
        SW_ASSERT_TRUE( reader.open( packPath ) );

        sw::vector<uint8> buffer;
        // CRC32 불일치 오류 로그와 함께 false를 반환해야 함
        SW_EXPECT_FALSE( reader.readFile( "secure/token.bin", buffer ) );
        SW_EXPECT_TRUE( buffer.empty() );
        reader.close();
    }

    sw::FileUtil::removeFile( packPath );
}

// ------------------------------------------------------------------------------
// Test 7: 멀티스레드 동시 VFS I/O 안전성 (Concurrent Multi-Threaded Read)
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_ResourcePack, ConcurrentMultiThreadedVfsRead )
{
    const sw::string packPath = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), "test_concurrent_vfs.pack" );

    sw::vector<std::pair<sw::string, sw::string>> listFile;
    for ( uint32 fileIndex = 0; fileIndex < 16; ++fileIndex )
    {
        listFile.push_back( { "data/file_" + sw::to_string( fileIndex ) + ".txt",
                              "CONTENT_PAYLOAD_OF_FILE_" + sw::to_string( fileIndex ) } );
    }

    SW_ASSERT_TRUE( sw::createTestPackFile( packPath, 0, sw::PackCompressionType::RLE, listFile ) );

    sw::ResourcePackManager manager;
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
                {
                    successCount.fetch_add( 1, std::memory_order_relaxed );
                }
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
    sw::FileUtil::removeFile( packPath );
}

// ------------------------------------------------------------------------------
// Test 8: 경로 캐시 64비트 정수 해시 룩업 및 무효화 (Path Cache Integrity)
// ------------------------------------------------------------------------------
SW_TEST_CASE( Engine_ResourcePack, PathCacheZeroAllocationAndInvalidation )
{
    sw::ResourceUtil::initialize();
    sw::ResourceUtil::clearPathCache();

    // 1. 존재하지 않는 경로 룩업
    const sw::string nonExistent = sw::ResourceUtil::getResourcePath( "non_existent_folder/file.unknown" );
    SW_EXPECT_TRUE( nonExistent.empty() );

    // 2. 엔진 폴더 내 알려진 경로 룩업 및 캐싱 확인
    const sw::string engineFolder = sw::ResourceUtil::getDomainFolderPath( "engine" );
    if ( engineFolder.empty() == false )
    {
        const sw::string pathFirst = sw::ResourceUtil::getResourcePath( "shaders" );
        // 캐시 히트 상태에서 100회 반복 룩업 일관성 검증
        for ( uint32 index = 0; index < 100; ++index )
        {
            const sw::string pathRepeat = sw::ResourceUtil::getResourcePath( "shaders" );
            SW_EXPECT_EQUAL( pathFirst, pathRepeat );
        }
    }

    // 3. 캐시 초기화 후 재동작 확인
    sw::ResourceUtil::clearPathCache();
}

/**
 * @brief [Engine_ResourcePack] 도메인 한정 경로 VFS 쿼리 검증 ("test_domain_pack/file.dat")
 */
SW_TEST_CASE( Engine_ResourcePack, DomainQualifiedQueryInVfs )
{
    const sw::string                                    packPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_domain_query_pack.pack" );
    const sw::vector<std::pair<sw::string, sw::string>> listFile = {
        { "textures/icon.dat",   "ICON_PAYLOAD_DATA"},
        {"shaders/custom.dat", "SHADER_PAYLOAD_DATA"},
    };

    SW_ASSERT_TRUE( sw::createTestPackFile( packPath, 0, sw::PackCompressionType::None, listFile ) );

    sw::ResourcePackManager packManager;
    packManager.setAllowLooseFiles( false ); // 순수 VFS 환경
    SW_ASSERT_TRUE( packManager.mountPack( packPath, 1000 ) );

    // 1. 도메인 없이 상대 경로로 쿼리
    SW_EXPECT_TRUE( packManager.hasFile( "textures/icon.dat" ) );
    sw::string textContent;
    SW_EXPECT_TRUE( packManager.readTextFile( "textures/icon.dat", textContent ) );
    SW_EXPECT_EQUAL( textContent, "ICON_PAYLOAD_DATA" );

    // 2. 도메인 접두사 포함 쿼리 ("sw_domain_query_pack/textures/icon.dat")
    SW_EXPECT_TRUE( packManager.hasFile( "sw_domain_query_pack/textures/icon.dat" ) );
    sw::string domainText;
    SW_EXPECT_TRUE( packManager.readTextFile( "sw_domain_query_pack/textures/icon.dat", domainText ) );
    SW_EXPECT_EQUAL( domainText, "ICON_PAYLOAD_DATA" );

    // 3. 존재하지 않는 도메인 쿼리 ("wrong_domain/textures/icon.dat")
    SW_EXPECT_FALSE( packManager.hasFile( "wrong_domain/textures/icon.dat" ) );

    // 4. 바이너리 도메인 쿼리 검증
    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( packManager.readFile( "sw_domain_query_pack/shaders/custom.dat", bytes ) );
    SW_EXPECT_EQUAL( bytes.size(), strlen( "SHADER_PAYLOAD_DATA" ) );

    packManager.unmountAll();
    sw::FileUtil::removeFile( packPath );
}
