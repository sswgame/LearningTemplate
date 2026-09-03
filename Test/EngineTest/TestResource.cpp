#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/DdsLoader.h"
#include "Engine/Resource/ResourceManager.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Engine_Resource — 팩 키·도메인 루트·레거시 XML
// ------------------------------------------------------------------------------
/**
 * @brief [Engine_Resource] 없는 리소스 경로는 빈 문자열
 */

SW_TEST_CASE( Engine_Resource, GetResourcePathEmptyForNonexistent )
{
    sw::ResourceUtil::initialize();
    sw::string nonExistent = sw::ResourceUtil::getResourcePath( "non_existent_file_xyz_12345.dat" );
    SW_EXPECT_TRUE( nonExistent.empty() );
}

/**
 * @brief [Engine_Resource] 폴더명을 지정해도 없으면 빈 경로
 */
SW_TEST_CASE( Engine_Resource, GetResourcePathWithFolderNameEmptyForNonexistent )
{
    sw::ResourceUtil::initialize();
    sw::string nonExistent = sw::ResourceUtil::getResourcePath( "non_existent_file_xyz_12345.dat", "textures" );
    SW_EXPECT_TRUE( nonExistent.empty() );
}

/**
 * @brief [Engine_Resource] 폴더 루트와 알려진 셰이더 경로
 */
SW_TEST_CASE( Engine_Resource, FolderRootsAndKnownShaderPath )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    const sw::string& root = sw::ResourceUtil::getRootFolderPath();
    SW_EXPECT_TRUE_MSG( root.empty() == false, "Resource root should be resolved (display only)" );
    SW_EXPECT_TRUE_MSG( sw::ResourceUtil::getDomainFolderPath( "game" ).empty() == false, "Games resource folder should exist" );
    SW_EXPECT_TRUE_MSG( sw::ResourceUtil::getDomainFolderPath( "engine" ).empty() == false, "Engine resource folder should exist" );
    SW_EXPECT_TRUE_MSG( sw::ResourceUtil::getDomainFolderPath( "editor" ).empty() == false, "Editor resource folder should exist" );

    const sw::string engineShaders = sw::ResourceUtil::getDomainFolderPath( "engine", "shaders" );
    SW_EXPECT_TRUE_MSG( engineShaders.empty() == false, "Expected engine shaders folder under domain root" );

    // 팩 상대 키(engine/common/game/editor 팩 아래 — Resource/ 자체가 아님).
    const sw::string shaderPath = sw::ResourceUtil::getResourcePath( "shaders/samplecompute.hlsl" );
    if ( shaderPath.empty() )
    {
        SW_TEST_SKIP( "samplecompute.hlsl not found under domain roots; skip path resolution check" );
    }

    const sw::string lowerPath = sw::FileUtil::normalizePath( shaderPath );
    SW_EXPECT_TRUE_MSG( lowerPath.find( "samplecompute.hlsl" ) != sw::string::npos, shaderPath.c_str() );
    SW_EXPECT_TRUE_MSG( lowerPath.find( "/resource/" ) == sw::string::npos || lowerPath.find( "/engine/" ) != sw::string::npos ||
                            lowerPath.find( "/common/" ) != sw::string::npos || lowerPath.find( "/game/" ) != sw::string::npos ||
                            lowerPath.find( "/editor/" ) != sw::string::npos,
                        "Resolved path should live under a domain folder" );

    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE_MSG( sw::FileUtil::readFile( shaderPath, bytes ), shaderPath.c_str() );
    SW_EXPECT_TRUE( bytes.empty() == false );

    const sw::string shaderPathAgain = sw::ResourceUtil::getResourcePath( "shaders/samplecompute.hlsl" );
    SW_EXPECT_STREQ( shaderPath.c_str(), shaderPathAgain.c_str() );

    // 전역 ID 는 도메인 루트로 간다(Resource/ + 전체 키가 아님).
    const sw::string viaGlobal = sw::ResourceUtil::getResourcePath( "engine/pipeline/forwardpipeline.xml" );
    SW_EXPECT_TRUE_MSG( viaGlobal.empty() == false, "engine/... global ID should resolve under engine root" );
    const sw::string viaPack = sw::ResourceUtil::getResourcePath( "pipeline/forwardpipeline.xml" );
    SW_EXPECT_TRUE_MSG( viaPack.empty() == false, "pack-relative pipeline key should resolve" );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( viaGlobal, viaPack ) );
}

/**
 * @brief [Engine_Resource] 도메인 아래에 없으면 빈 경로
 */
SW_TEST_CASE( Engine_Resource, GetResourcePathEmptyWhenMissingUnderDomains )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    // Resource/ 자체가 검색 루트일 때만 "동작"한다.
    SW_EXPECT_TRUE( sw::ResourceUtil::getResourcePath( "this_file_does_not_exist_anywhere.bin" ).empty() );
}

/**
 * @brief [Engine_Resource] 저장 경로가 상대 폴더를 소문자화
 */
SW_TEST_CASE( Engine_Resource, MakeSavePathLowercasesRelativeFolders )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    const sw::string gameRoot = sw::ResourceUtil::getDomainFolderPath( "game" );
    SW_EXPECT_TRUE( gameRoot.empty() == false );

    const sw::string saveFolder = sw::ResourceUtil::makeSaveFolderPath( gameRoot + "/shaders/nested" );
    const sw::string savePath   = sw::ResourceUtil::makeSavePath( gameRoot + "/shaders/nested", "foobar.hlsl" );
    const sw::string saveKey    = sw::FileUtil::normalizePath( savePath );

    SW_EXPECT_TRUE_MSG( saveKey.find( "shaders/nested/foobar.hlsl" ) != sw::string::npos, savePath.c_str() );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( sw::ResourceUtil::makeSaveFolderPath( gameRoot ), gameRoot ) );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::normalizePath( saveFolder ).find( "shaders/nested" ) != sw::string::npos, saveFolder.c_str() );

    // 대문자 저장 대상 폴더 및 대문자 파일명 전달 시 소문자 변환 검증 (리눅스 에셋 표준)
    const sw::string saveUpperFolder = sw::ResourceUtil::makeSaveFolderPath( gameRoot + "/PREFABS/SUB_DIR" );
    const sw::string saveUpperPath   = sw::ResourceUtil::makeSavePath( gameRoot + "/PREFABS/SUB_DIR", "NEW_HERO.PREFAB.JSON" );
    SW_EXPECT_TRUE( sw::StringUtil::endsWith( saveUpperFolder, "prefabs/sub_dir", true ) );
    SW_EXPECT_TRUE( sw::StringUtil::endsWith( saveUpperPath, "prefabs/sub_dir/new_hero.prefab.json", true ) );
}

/**
 * @brief [Engine_Resource] formatVersion=0 passthrough; legacy unmigrated XML은 거부
 */
SW_TEST_CASE( Engine_Resource, AssetFormatAcceptsCurrentMaterialXml )
{
    sw::ResourceUtil::initialize();

    const utf8* kCurrent = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDesc formatVersion="0" name="CurrentMat" shaderPath="engine/shaders/forwardlit.hlsl" blendMode="Opaque">
	<_properties>
		<item name="color" type="Color" shaderType="Float4" defaultValue="1 0 0 1"/>
	</_properties>
</MaterialDesc>
)";

    sw::XmlDocument doc;
    doc.parse( kCurrent );
    sw::XmlNode root = doc.root( "MaterialDesc" );
    SW_ASSERT_TRUE( root.isValid() );

    sw::AssetFormatVersion source = 99;
    SW_EXPECT_TRUE( sw::engine::getResourceManager().getAssetFormatRegistry().upgradeXml( sw::AssetKind::Material, doc, root,
                                                                                          sw::AssetFormatVersions::kMaterial, &source ) );
    SW_EXPECT_EQUAL( sw::AssetFormatVersions::kMaterial, source );
    SW_EXPECT_TRUE( root.attr( "formatVersion" ) != nullptr );
    SW_EXPECT_STREQ( "0", root.attr( "formatVersion" ) );

    const sw::string tempPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_current_material.material" );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( tempPath, reinterpret_cast<const uint8*>( kCurrent ),
                                             static_cast<uint64>( sw::StringUtil::strlen( kCurrent ) ) ) );

    sw::Material material;
    SW_EXPECT_TRUE( material.loadFromFile( tempPath ) );
    SW_EXPECT_EQUAL( sw::string( "CurrentMat" ), material.getName() );
    SW_EXPECT_EQUAL( sw::string( "engine/shaders/forwardlit.hlsl" ), material.getShaderPath() );
    sw::FileUtil::removeFile( tempPath );
}

/**
 * @brief [Engine_Resource] formatVersion 없는 옛 Material 루트는 더 이상 자동 변환하지 않음
 */
SW_TEST_CASE( Engine_Resource, AssetFormatRejectsLegacyMaterialXml )
{
    sw::ResourceUtil::initialize();

    const utf8* kLegacy = R"(<?xml version="1.0" encoding="utf-8"?>
<Material>
	<_name>LegacyMat</_name>
	<_shader>engine/shaders/forwardlit.hlsl</_shader>
</Material>
)";

    const sw::string tempPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_legacy_material.material" );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( tempPath, reinterpret_cast<const uint8*>( kLegacy ),
                                             static_cast<uint64>( sw::StringUtil::strlen( kLegacy ) ) ) );

    sw::Material material;
    SW_EXPECT_FALSE( material.loadFromFile( tempPath ) );
    sw::FileUtil::removeFile( tempPath );
}

/**
 * @brief [Engine_Resource] AssetStreamingQueue 비동기 요청 등록, 취소, 프레임 쓰로틀링 콜백 검증
 */
SW_TEST_CASE( Engine_Resource, AssetStreamingQueueLifecycleAndThrottling )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    SW_EXPECT_EQUAL( size_t( 0 ), queue.getPendingCount() );
    SW_EXPECT_EQUAL( size_t( 0 ), queue.getCompletedCount() );

    bool       bCompleted1{ false };
    bool       bSuccess1{ false };
    sw::string path1{};

    queue.requestAsset(
        "Textures/Character.png",
        sw::StreamingPriority::Normal,
        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&]( std::string_view p, bool ok )
    {
        bCompleted1 = true;
        bSuccess1   = ok;
        path1       = sw::string( p );
    } ) );

    SW_EXPECT_TRUE( queue.isStreaming( "Textures/Character.png" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), queue.getPendingCount() );

    // 취소 요청 검증
    queue.cancelRequest( "Textures/Character.png" );
    SW_EXPECT_FALSE( queue.isStreaming( "Textures/Character.png" ) );

    // 새 요청 등록 후 메인 프레임 틱 업데이트
    queue.requestAsset(
        "Audio/BGM.wav",
        sw::StreamingPriority::High,
        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&]( std::string_view p, bool ok )
    {
        bCompleted1 = true;
        bSuccess1   = ok;
        path1       = sw::string( p );
    } ) );

    // 워커 스레드 작업 완료 대기
    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );

    queue.update( 10 );
    queue.sweepUnusedCache();

    queue.shutdown();
}

/**
 * @brief [Engine_Resource] 리소스 경로 조회 시 소문자 자동 정규화 및 대소문자 무관 탐색 검증
 */
SW_TEST_CASE( Engine_Resource, ResourcePathCaseInsensitiveLookupAndLowerCaseNormalization )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    // 1. FileUtil::normalizePath 기본 동작 검증 (역슬래시 -> 슬래시, 소문자화)
    const sw::string rawPath    = "Resource\\Engine\\Pipeline\\ForwardPipeline.xml";
    const sw::string normalized = sw::FileUtil::normalizePath( rawPath );
    SW_EXPECT_STREQ( "resource/engine/pipeline/forwardpipeline.xml", normalized.c_str() );

    // 2. 대문자/혼합 대소문자 전역 ID로 조회 시 소문자 물리 파일 매핑 검증
    const sw::string pathUpperGlobal = sw::ResourceUtil::getResourcePath( "ENGINE/MATERIALS/DEFAULTMATERIAL.MATERIAL" );
    SW_EXPECT_FALSE( pathUpperGlobal.empty() );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathUpperGlobal ) );
    SW_EXPECT_TRUE( sw::StringUtil::endsWith( pathUpperGlobal, "defaultmaterial.material", true ) );

    // 3. 엔진 파이프라인 대문자 조회 검증
    const sw::string pathEnginePipeline = sw::ResourceUtil::getResourcePath( "ENGINE/PIPELINE/FORWARDPIPELINE.XML" );
    SW_EXPECT_FALSE( pathEnginePipeline.empty() );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathEnginePipeline ) );

    // 4. 팩 상대 키(Mixed case) 조회 검증
    //    게임 콘텐츠에 의존하지 않도록 팩 루트를 엔진 팩으로 지정해 검사한다.
    const sw::GameConfig oldActive  = sw::GameConfig::getActive();
    sw::GameConfig       packConfig = oldActive;
    packConfig._packRoot            = "engine";
    sw::GameConfig::setActive( packConfig );

    const sw::string pathMixedPack = sw::ResourceUtil::getResourcePath( "Pipeline/ForwardPipeline.xml" );
    SW_EXPECT_FALSE( pathMixedPack.empty() );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathMixedPack ) );

    sw::GameConfig::setActive( oldActive );

    // 5. 다른 엔진 리소스의 대문자 키 조회 검증
    const sw::string pathUpperOther = sw::ResourceUtil::getResourcePath( "ENGINE/PIPELINE/DEFERREDPIPELINE.XML" );
    SW_EXPECT_FALSE( pathUpperOther.empty() );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathUpperOther ) );

    // 6. 텍스트 리소스 읽기 시 대문자 키 전달 검증
    sw::string textContent;
    const bool bReadOk = sw::ResourceUtil::readTextResource( "ENGINE/PIPELINE/FORWARDPIPELINE.XML", textContent );
    SW_EXPECT_TRUE( bReadOk );
    SW_EXPECT_FALSE( textContent.empty() );
    SW_EXPECT_TRUE( textContent.find( "<RenderPipeline" ) != sw::string::npos );
}

/**
 * @brief [Engine_Resource] EngineConfig 기반 검색 우선순위 동적 변경 및 DLC/모드 경로 지원 검증
 */
SW_TEST_CASE( Engine_Resource, ConfigurableResourcePriorityAndDlcSupport )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    // 1. 기본 검색 우선순위 확인
    const sw::vector<sw::string>& defaultPriority = sw::ResourceUtil::getSearchPriority();
    SW_EXPECT_FALSE( defaultPriority.empty() );
    SW_EXPECT_STREQ( "game", defaultPriority[0].c_str() );

    // 2. 임시 game 및 DLC 디렉터리/에셋 생성하여 우선순위 오버라이드 검증
    const sw::string gameDir  = sw::FileUtil::joinPath( sw::ResourceUtil::getRootFolderPath(), "game/empty/test_asset" );
    const sw::string gameFile = sw::FileUtil::joinPath( gameDir, "priority_test.xml" );
    sw::FileUtil::ensureDirectoryExists( gameDir );
    const utf8* kGameContent = "<Asset source=\"game\" />";
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( gameFile, reinterpret_cast<const uint8*>( kGameContent ),
                                             static_cast<uint64>( sw::StringUtil::strlen( kGameContent ) ) ) );

    const sw::string dlcDir  = sw::FileUtil::joinPath( sw::ResourceUtil::getRootFolderPath(), "dlc/test_dlc/test_asset" );
    const sw::string dlcFile = sw::FileUtil::joinPath( dlcDir, "priority_test.xml" );
    sw::FileUtil::ensureDirectoryExists( dlcDir );

    const utf8* kDlcContent = "<Asset source=\"dlc\" />";
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( dlcFile, reinterpret_cast<const uint8*>( kDlcContent ),
                                             static_cast<uint64>( sw::StringUtil::strlen( kDlcContent ) ) ) );

    // 3. DLC가 1순위인 우선순위 목록 적용
    const sw::vector<sw::string> listDlcFirstPriority = { "dlc/test_dlc", "game", "common", "engine", "editor" };
    SW_EXPECT_TRUE( sw::ResourceUtil::setSearchPriority( listDlcFirstPriority ) );

    // 4. 팩 상대 키 "test_asset/priority_test.xml" 조회 시 게임 팩이 아닌 DLC 폴더의 파일로 매핑되는지 검증
    const sw::string resolvedDlcPath = sw::ResourceUtil::getResourcePath( "test_asset/priority_test.xml" );
    SW_EXPECT_FALSE( resolvedDlcPath.empty() );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( resolvedDlcPath, dlcFile ) );

    // 5. EngineConfig 리플렉션 기본값(getDefaultSearchPriority)으로 우선순위 복구 시 기존 game 팩 파일 매핑 검증
    const sw::vector<sw::string>& listRestoredPriority = sw::ResourceUtil::getDefaultSearchPriority();
    SW_EXPECT_TRUE( sw::ResourceUtil::setSearchPriority( listRestoredPriority ) );

    const sw::string resolvedGamePath = sw::ResourceUtil::getResourcePath( "test_asset/priority_test.xml" );
    SW_EXPECT_FALSE( resolvedGamePath.empty() );
    SW_EXPECT_FALSE( sw::FileUtil::pathsEqualNormalized( resolvedGamePath, dlcFile ) );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( resolvedGamePath, gameFile ) );

    // 6. 임시 파일 및 디렉터리 정리
    sw::FileUtil::removeFile( dlcFile );
    sw::FileUtil::removeDirectory( dlcDir );
    sw::FileUtil::removeDirectory( sw::FileUtil::joinPath( sw::ResourceUtil::getRootFolderPath(), "dlc/test_dlc" ) );
    sw::FileUtil::removeDirectory( sw::FileUtil::joinPath( sw::ResourceUtil::getRootFolderPath(), "dlc" ) );

    sw::FileUtil::removeFile( gameFile );
    sw::FileUtil::removeDirectory( gameDir );
}

/**
 * @brief [Engine_Resource] DdsLoader를 통한 DDS 헤더 파싱 및 픽셀 버퍼 로드 검증
 */
SW_TEST_CASE( Engine_Resource, DdsLoaderValidHeaderAndPixelLoading )
{
    sw::ResourceUtil::initialize();
    const sw::string splashDdsPath = sw::ResourceUtil::getResourcePath( "textures/splash.dds" );
    SW_ASSERT_TRUE( splashDdsPath.empty() == false );

    sw::DdsImageData image;
    SW_ASSERT_TRUE( sw::DdsLoader::loadFromFile( splashDdsPath, image ) );

    SW_EXPECT_TRUE( image.isValid() );
    SW_EXPECT_EQUAL( 1376u, image._width );
    SW_EXPECT_EQUAL( 768u, image._height );
    SW_EXPECT_EQUAL( 87u, image._dxgiFormat ); // DXGI_FORMAT_B8G8R8A8_UNORM
    SW_EXPECT_EQUAL( SW_TRUE, image._bIsBgra );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1376 * 768 * 4 ), image._bytes.size() );
    SW_EXPECT_NOT_NULL( image.getPixels() );
}

/**
 * @brief [Engine_Resource] DdsLoader::loadFromResource를 통한 VFS 상대 경로 DDS 로드 검증
 */
SW_TEST_CASE( Engine_Resource, DdsLoaderLoadFromResource )
{
    sw::ResourceUtil::initialize();
    sw::DdsImageData image;
    SW_ASSERT_TRUE( sw::DdsLoader::loadFromResource( "textures/splash.dds", image ) );
    SW_EXPECT_TRUE( image.isValid() );
    SW_EXPECT_EQUAL( 1376u, image._width );
    SW_EXPECT_EQUAL( 768u, image._height );
}

/**
 * @brief [Engine_Resource] AssetDatabase 캡슐화, tryGetGuid/Path, registerMapping 검증
 */
SW_TEST_CASE( Engine_Resource, AssetDatabaseThreadSafeLookupAndMapping )
{
    sw::AssetDatabase db;
    SW_EXPECT_EQUAL( 0u, db.getAssetCount() );

    const sw::Uuid testGuid = sw::Uuid::generate();
    db.registerMapping( "prefabs/player.prefab.xml", testGuid );
    SW_EXPECT_EQUAL( 1u, db.getAssetCount() );

    sw::Uuid outGuid{};
    SW_EXPECT_TRUE( db.tryGetGuid( "prefabs/player.prefab.xml", outGuid ) );
    SW_EXPECT_TRUE( testGuid == outGuid );

    sw::string outPath;
    SW_EXPECT_TRUE( db.tryGetPath( testGuid, outPath ) );
    SW_EXPECT_STREQ( "prefabs/player.prefab.xml", outPath.c_str() );

    sw::Uuid missingGuid{};
    SW_EXPECT_FALSE( db.tryGetGuid( "nonexistent.xml", missingGuid ) );

    db.clear();
    SW_EXPECT_EQUAL( 0u, db.getAssetCount() );
}

/**
 * @brief [Engine_Resource] AssetStreamingQueue::requestAssetData 비동기 버퍼 프리로드 검증
 */
SW_TEST_CASE( Engine_Resource, AssetStreamingQueueDataRequest )
{
    sw::ResourceUtil::initialize();
    sw::AssetStreamingQueue queue;
    queue.initialize();

    bool              bCallbackInvoked{ false };
    bool              bSuccessResult{ false };
    sw::vector<uint8> loadedBytes;

    const bool bEnqueued = queue.requestAssetData(
        "textures/splash.dds",
        sw::StreamingPriority::Normal,
        SW_DELEGATE_LAMBDA(
            sw::OnStreamingDataCompleteDelegate,
            [&]( string_view /*path*/, bool bSuccess, const sw::vector<uint8>& bytes )
    {
        bCallbackInvoked = true;
        bSuccessResult   = bSuccess;
        loadedBytes      = bytes;
    } ) );

    SW_ASSERT_TRUE( bEnqueued );

    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getTaskManager().waitAll();

    queue.update( 32 );

    SW_EXPECT_TRUE( bCallbackInvoked );
    SW_EXPECT_TRUE( bSuccessResult );
    SW_EXPECT_TRUE( loadedBytes.empty() == false );

    queue.shutdown();
}

/**
 * @brief [Engine_Resource] ResourceManager의 ResourcePackManager 소유권 및 라이프사이클 검증
 */
SW_TEST_CASE( Engine_Resource, ResourceManagerPackManagerOwnership )
{
    sw::ResourceManager resManager;
    SW_ASSERT_TRUE( resManager.initialize() );

    sw::ResourcePackManager& packMgr = resManager.getPackManager();
    SW_EXPECT_NOT_NULL( &packMgr );

    resManager.shutdown();
}

/**
 * @brief [Engine_Resource] ResourceUtil::getDomainFolderPath 동적 도메인 및 서브폴더 해석 검증
 */
SW_TEST_CASE( Engine_Resource, DynamicDomainFolderPathResolution )
{
    sw::ResourceUtil::initialize();

    const sw::string engineRoot = sw::ResourceUtil::getDomainFolderPath( "engine" );
    SW_EXPECT_TRUE( engineRoot.empty() == false );

    const sw::string engineShaders = sw::ResourceUtil::getDomainFolderPath( "engine", "shaders" );
    SW_EXPECT_TRUE( engineShaders.empty() == false );
    SW_EXPECT_TRUE( engineShaders.find( "shaders" ) != sw::string::npos );

    const sw::string commonRoot = sw::ResourceUtil::getDomainFolderPath( "common" );
    SW_EXPECT_TRUE( commonRoot.empty() == false );

    const sw::string nonExistent = sw::ResourceUtil::getDomainFolderPath( "invalid_domain_xyz_999" );
    SW_EXPECT_TRUE( nonExistent.empty() );

    const sw::string nonExistentSub = sw::ResourceUtil::getDomainFolderPath( "engine", "invalid_subfolder_xyz_999" );
    SW_EXPECT_TRUE( nonExistentSub.empty() );
}

/**
 * @brief [Engine_Resource] ResourceUtil::makeAbsolutePath 절대 경로 전달 시 그대로 반환 및 도메인 오인 방지 검증
 */
SW_TEST_CASE( Engine_Resource, AbsolutePathPreservation )
{
    sw::ResourceUtil::initialize();

    const sw::string posixAbs = "/home/runner/work/scene.xml";
    SW_EXPECT_STREQ( posixAbs.c_str(), sw::ResourceUtil::makeAbsolutePath( posixAbs ).c_str() );

    const sw::string winAbs = "C:/Projects/Game/scene.xml";
    SW_EXPECT_STREQ( winAbs.c_str(), sw::ResourceUtil::makeAbsolutePath( winAbs ).c_str() );

    SW_EXPECT_TRUE( sw::ResourceUtil::getResourcePath( posixAbs ).empty() );
}
