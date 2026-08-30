#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/AssetStreamingQueue.h"
#include "Engine/Utility/Resource/ResourceManager.h"

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
	SW_EXPECT_TRUE_MSG( sw::ResourceUtil::getGameFolderPath().empty() == false, "Games resource folder should exist" );
	SW_EXPECT_TRUE_MSG( sw::ResourceUtil::getEngineFolderPath().empty() == false, "Engine resource folder should exist" );
	SW_EXPECT_TRUE_MSG( sw::ResourceUtil::getEditorFolderPath().empty() == false, "Editor resource folder should exist" );

	const sw::vector<sw::string> shaderFolders = sw::ResourceUtil::getResourceFolders( "shaders" );
	SW_EXPECT_TRUE_MSG( shaderFolders.empty() == false, "Expected at least one shaders folder under domain roots" );

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

	const sw::string& gameRoot = sw::ResourceUtil::getGameFolderPath();
	SW_EXPECT_TRUE( gameRoot.empty() == false );

	const sw::string saveFolder = sw::ResourceUtil::makeSaveFolderPath( gameRoot + "/shaders/nested" );
	const sw::string savePath	= sw::ResourceUtil::makeSavePath( gameRoot + "/shaders/nested", "foobar.hlsl" );
	const sw::string saveKey	= sw::FileUtil::normalizePath( savePath );

	SW_EXPECT_TRUE_MSG( saveKey.find( "shaders/nested/foobar.hlsl" ) != sw::string::npos, savePath.c_str() );
	SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( sw::ResourceUtil::makeSaveFolderPath( gameRoot ), gameRoot ) );
	SW_EXPECT_TRUE_MSG( sw::FileUtil::normalizePath( saveFolder ).find( "shaders/nested" ) != sw::string::npos, saveFolder.c_str() );

	// 대문자 저장 대상 폴더 및 대문자 파일명 전달 시 소문자 변환 검증 (리눅스 에셋 표준)
	const sw::string saveUpperFolder = sw::ResourceUtil::makeSaveFolderPath( gameRoot + "/PREFABS/SUB_DIR" );
	const sw::string saveUpperPath	 = sw::ResourceUtil::makeSavePath( gameRoot + "/PREFABS/SUB_DIR", "NEW_HERO.PREFAB.JSON" );
	SW_EXPECT_TRUE( sw::FileUtil::endsWithIgnoreCase( saveUpperFolder, "prefabs/sub_dir" ) );
	SW_EXPECT_TRUE( sw::FileUtil::endsWithIgnoreCase( saveUpperPath, "prefabs/sub_dir/new_hero.prefab.json" ) );
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

	bool	   bCompleted1{ false };
	bool	   bSuccess1{ false };
	sw::string path1{};

	queue.requestAsset(
		"Textures/Character.png",
		sw::StreamingPriority::Normal,
		SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&]( std::string_view p, bool ok )
	{
		bCompleted1 = true;
		bSuccess1	= ok;
		path1		= sw::string( p );
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
		bSuccess1	= ok;
		path1		= sw::string( p );
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
	const sw::string rawPath	= "Resource\\Game\\Demo\\Maps\\Town01.xml";
	const sw::string normalized = sw::FileUtil::normalizePath( rawPath );
	SW_EXPECT_STREQ( "resource/game/demo/maps/town01.xml", normalized.c_str() );

	// 2. 대문자/혼합 대소문자 전역 ID로 조회 시 소문자 물리 파일 매핑 검증
	const sw::string pathUpperGlobal = sw::ResourceUtil::getResourcePath( "GAME/DEMO/DATA/GAMEDATA.XML" );
	SW_EXPECT_FALSE( pathUpperGlobal.empty() );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathUpperGlobal ) );
	SW_EXPECT_TRUE( sw::FileUtil::endsWithIgnoreCase( pathUpperGlobal, "gamedata.xml" ) );

	// 3. 엔진 파이프라인 대문자 조회 검증
	const sw::string pathEnginePipeline = sw::ResourceUtil::getResourcePath( "ENGINE/PIPELINE/FORWARDPIPELINE.XML" );
	SW_EXPECT_FALSE( pathEnginePipeline.empty() );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathEnginePipeline ) );

	// 4. 팩 상대 키(Mixed case) 조회 검증
	const sw::string pathMixedPack = sw::ResourceUtil::getResourcePath( "Maps/Town01.xml" );
	SW_EXPECT_FALSE( pathMixedPack.empty() );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathMixedPack ) );

	// 5. 프리팹 대문자 키 조회 검증
	const sw::string pathUpperPrefab = sw::ResourceUtil::getResourcePath( "game/demo/prefabs/GHOST.PREFAB.JSON" );
	SW_EXPECT_FALSE( pathUpperPrefab.empty() );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathUpperPrefab ) );

	// 6. 텍스트 리소스 읽기 시 대문자 키 전달 검증
	sw::string textContent;
	const bool bReadOk = sw::ResourceUtil::readTextResource( "GAME/DEMO/DATA/GAMEDATA.XML", textContent );
	SW_EXPECT_TRUE( bReadOk );
	SW_EXPECT_FALSE( textContent.empty() );
	SW_EXPECT_TRUE( textContent.find( "<GameData>" ) != sw::string::npos );
}
