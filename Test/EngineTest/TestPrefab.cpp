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

		/** @brief 테스트용 샘플 XML 프리팹 파일을 생성하고 경로를 반환합니다. */
		sw::string ensureSamplePrefabXml()
		{
			const sw::string path = makeTempPrefabPath( "sample_source.prefab.xml" );
			if ( sw::FileUtil::fileExists( path ) == false )
			{
				const utf8* pXmlContent = R"(<?xml version="1.0" encoding="utf-8"?>
<Prefab version="1" name="SampleHero">
    <GameObject _name="SampleHero">
    </GameObject>
</Prefab>)";
				sw::FileUtil::writeTextFile( path, pXmlContent );
			}
			return path;
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
	const sw::string srcXmlPath = sw::ensureSamplePrefabXml();
	sw::PrefabAsset	 src;
	SW_ASSERT_TRUE( src.loadFromXmlFile( srcXmlPath ) );
	SW_EXPECT_TRUE( src.isValid() );
	SW_EXPECT_FALSE( src.getStateData().empty() );

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

	sw::FileUtil::removeFile( xmlPath );
	sw::FileUtil::removeFile( jsonPath );
	sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [PrefabTest] 경로 구분자·확장자가 달라도 같은 캐시 엔트리
 */
SW_TEST_CASE( PrefabTest, CacheKeyNormalizesPathAndExtension )
{
	const sw::string srcXmlPath = sw::ensureSamplePrefabXml();
	sw::PrefabAsset	 src;
	SW_ASSERT_TRUE( src.loadFromXmlFile( srcXmlPath ) );

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
	const sw::string	  srcXmlPath = sw::ensureSamplePrefabXml();
	sw::GameObjectManager objects;
	sw::PrefabManager	  prefabs;
	sw::GameObject*		  spawned = prefabs.spawn( &objects, srcXmlPath, "SpawnedSample" );
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
	const utf8* prefabJson = R"({
		"_name": "DynamicPrefabActor"
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

/**
 * @brief [PrefabTest] 프리팹 자기 참조 및 순환 참조 스폰 시 스택 오버플로우 방어 검증
 */
SW_TEST_CASE( PrefabTest, CircularReferenceSpawnProtection )
{
#if defined( SW_SHIPPING )
	SW_TEST_SKIP( "Circular prefab spawn test is Dev-only" );
#else
	const sw::string tempPath	= sw::makeTempPrefabPath( "circular_self.prefab.json" );
	const sw::string prefabJson = sw::string( R"({
		"_name": "CircularSelf",
		"_prefabAssetPath": ")" ) +
								  tempPath + R"("
	})";

	SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( tempPath, prefabJson ) );

	sw::GameObjectManager objects;
	sw::PrefabManager	  prefabs;

	// 순환 참조 감지 시 무한 재귀 없이 안전하게 반환
	sw::GameObject* spawned = prefabs.spawn( &objects, tempPath, "TestCircular" );
	SW_ASSERT_NOT_NULL( spawned );

	sw::FileUtil::removeFile( tempPath );
#endif
}
