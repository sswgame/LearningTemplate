#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
	namespace
	{
		/** @brief 실행 파일 옆 임시 프리팹 경로를 만듭니다. */
		sw::string makeTempPrefabPath( const std::string_view fileName )
		{
			sw::string dir = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
			dir += "/prefab_test";
			sw::FileUtil::ensureDirectoryExists( dir );
			dir += "/";
			dir += fileName;
			return sw::FileUtil::normalizePath( dir );
		}

	} // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) PrefabTest — 라운드트립·캐시 키·스폰
// ------------------------------------------------------------------------------
/**
 * @brief [PrefabTest] XML 로드 후 JSON/binary 라운드트립
 */
SW_TEST_CASE( PrefabTest, XmlJsonBinaryRoundtrip )
{
	sw::PrefabAsset src;
	if ( src.loadFromXmlFile( "prefabs/sample.prefab.xml" ) == false )
	{
		SW_TEST_SKIP( "prefabs/sample.prefab.xml not found" );
	}
	SW_EXPECT_TRUE( src.isValid() );
	SW_EXPECT_FALSE( src.getXmlBody().empty() );

	const sw::string xmlPath  = sw::makeTempPrefabPath( "roundtrip.prefab.xml" );
	const sw::string jsonPath = sw::makeTempPrefabPath( "roundtrip.prefab.json" );
	const sw::string binPath  = sw::makeTempPrefabPath( "roundtrip.prefab.bin" );

	SW_EXPECT_TRUE( src.saveToXmlFile( xmlPath ) );
	SW_EXPECT_TRUE( src.saveToJsonFile( jsonPath ) );
	SW_EXPECT_TRUE( src.saveToBinaryFile( binPath ) );

	sw::PrefabAsset fromXml;
	SW_EXPECT_TRUE( fromXml.loadFromXmlFile( xmlPath ) );
	SW_EXPECT_TRUE( fromXml.isValid() );
	SW_EXPECT_EQUAL( src.getName(), fromXml.getName() );

	sw::PrefabAsset fromJson;
	SW_EXPECT_TRUE( fromJson.loadFromJsonFile( jsonPath ) );
	SW_EXPECT_TRUE( fromJson.isValid() );
	SW_EXPECT_EQUAL( src.getName(), fromJson.getName() );

	sw::PrefabAsset fromBin;
	SW_EXPECT_TRUE( fromBin.loadFromBinaryFile( binPath ) );
	SW_EXPECT_TRUE( fromBin.isValid() );
	SW_EXPECT_EQUAL( src.getName(), fromBin.getName() );
}

/**
 * @brief [PrefabTest] 경로 구분자·확장자가 달라도 같은 캐시 엔트리
 */
SW_TEST_CASE( PrefabTest, CacheKeyNormalizesPathAndExtension )
{
	sw::PrefabAsset src;
	if ( src.loadFromXmlFile( "prefabs/sample.prefab.xml" ) == false )
	{
		SW_TEST_SKIP( "prefabs/sample.prefab.xml not found" );
	}

	const sw::string xmlPath  = sw::makeTempPrefabPath( "cachekey.prefab.xml" );
	const sw::string jsonPath = sw::makeTempPrefabPath( "cachekey.prefab.json" );
	const sw::string binPath  = sw::makeTempPrefabPath( "cachekey.prefab.bin" );
	SW_EXPECT_TRUE( src.saveToXmlFile( xmlPath ) );
	SW_EXPECT_TRUE( src.saveToJsonFile( jsonPath ) );
	SW_EXPECT_TRUE( src.saveToBinaryFile( binPath ) );

	sw::PrefabManager manager;
	sw::PrefabAsset*  fromXml  = manager.loadPrefab( xmlPath );
	sw::PrefabAsset*  fromJson = manager.loadPrefab( jsonPath );
	SW_ASSERT_NOT_NULL( fromXml );
	SW_EXPECT_EQUAL( fromXml, fromJson );

	sw::string slashFlipped = xmlPath;
	for ( utf8& ch : slashFlipped )
	{
		if ( ch == '/' )
			ch = '\\';
		else if ( ch == '\\' )
			ch = '/';
	}
	if ( slashFlipped != xmlPath )
		SW_EXPECT_EQUAL( fromXml, manager.loadPrefab( slashFlipped ) );

	sw::FileUtil::removeFile( xmlPath );
	sw::FileUtil::removeFile( jsonPath );
	sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [PrefabTest] spawn 이 GameObject 를 만든다
 */
SW_TEST_CASE( PrefabTest, SpawnCreatesGameObject )
{
	if ( sw::ResourceUtil::getResourcePath( "prefabs/sample.prefab.xml" ).empty() )
	{
		SW_TEST_SKIP( "prefabs/sample.prefab.xml not found" );
	}

	sw::GameObjectManager objects;
	sw::PrefabManager	  prefabs;
	sw::GameObject*		  spawned = prefabs.spawn( &objects, "prefabs/sample.prefab.xml", "SpawnedSample" );
	SW_ASSERT_NOT_NULL( spawned );
	SW_EXPECT_EQUAL( sw::string( "SpawnedSample" ), sw::string( spawned->getName().c_str() ) );
}

/**
 * @brief [PrefabTest] 인메모리 JSON 프리팹 에셋 생성, 파일 저장 및 스폰 검증
 */
SW_TEST_CASE( PrefabTest, InMemoryJsonPrefabCreationAndSpawn )
{
#if defined( SW_SHIPPING )
	SW_TEST_SKIP( "InMemory JSON prefab spawn is Dev-only (Shipping requires cooked .bin)" );
#else
	const char* prefabJson = R"({
		"Name": "DynamicPrefabActor"
	})";

	const sw::string tempPath = sw::makeTempPrefabPath( "in_memory_test.prefab.json" );
	SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( tempPath, prefabJson ) );

	sw::GameObjectManager objects;
	sw::PrefabManager	  prefabs;

	sw::GameObject* spawned = prefabs.spawn( &objects, tempPath, "BossActor" );
	SW_ASSERT_NOT_NULL( spawned );
	SW_EXPECT_EQUAL( sw::string( "BossActor" ), sw::string( spawned->getName().c_str() ) );

	sw::FileUtil::removeFile( tempPath );
#endif
}
