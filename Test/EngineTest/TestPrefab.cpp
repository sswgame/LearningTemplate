#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief 테스트용 샘플 XML 프리팹 파일을 이 케이스의 임시 경로에 만들고 경로를 반환합니다. */
        sw::string ensureSamplePrefabXml()
        {
            const sw::string path = test::makeTempPath( "sample_source.prefab.xml" );
            if ( sw::FileUtil::fileExists( path ) == false )
            {
                const utf8* pXmlContent = R"(<?xml version="1.0" encoding="utf-8"?>
<Prefab version="1" name="SampleHero">
    <GameObject _name="SampleHero">
    </GameObject>
</Prefab>)";
                sw::FileUtil::writeTextFile( path, pXmlContent );
            }

            // 쿠킹본(.prefab.bin)도 같이 만들어 둔다. PrefabManager::getOrLoad 는 Shipping 에서
            // **바이너리만** 읽으므로(XML 은 Dev 전용 경로다), XML 만 두면 스폰 검증이 배포 빌드에서
            // 그냥 죽는다. 쿠커가 하는 일과 같은 변환을 테스트가 자기 손으로 해 둔다.
            sw::string binPath = path;
            binPath.replace( binPath.size() - 4, 4, ".bin" );
            if ( sw::FileUtil::fileExists( binPath ) == false )
            {
                sw::PrefabAsset cooked;
                if ( cooked.loadFromXmlFile( path ) )
                    cooked.saveToBinaryFile( binPath );
            }
            return path;
        }

        /** @brief 씬 컴포넌트 하나(위치 1,2,3 · 스케일 2)를 든 프리팹 에셋을 만듭니다. */
        sw::PrefabAsset makeCratePrefab()
        {
            sw::GameObjectManager authoring;
            sw::GameObject*       pSource = authoring.createGameObject( sw::hashed_string( "Crate" ) );
            sw::SceneComponent*   pRoot   = pSource->addComponent<sw::SceneComponent>();
            pRoot->setLocalPosition( sw::float3( 1.0f, 2.0f, 3.0f ) );
            pRoot->setLocalScale( sw::float3( 2.0f, 2.0f, 2.0f ) );
            sw::PrefabAsset asset;
            asset.setFromGameObject( pSource );
            return asset;
        }

        /** @brief 저작 파일(@p sourcePath)과 같은 이름의 쿠킹본(.bin)을 씁니다 — Shipping 은 쿠킹본만 읽습니다. */
        bool writeCookedBeside( const sw::string& sourcePath, bool bJson )
        {
            sw::PrefabAsset cooked;
            if ( ( bJson ? cooked.loadFromJsonFile( sourcePath ) : cooked.loadFromXmlFile( sourcePath ) ) == false )
                return false;
            return cooked.saveToBinaryFile( sw::FileUtil::replaceExtension( sourcePath, ".bin" ) );
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
    sw::PrefabAsset  src;
    SW_ASSERT_TRUE( src.loadFromXmlFile( srcXmlPath ) );
    SW_EXPECT_TRUE( src.isValid() );
    SW_EXPECT_FALSE( src.getStateData().empty() );

    const sw::string xmlPath  = test::makeTempPath( "roundtrip.prefab.xml" );
    const sw::string jsonPath = test::makeTempPath( "roundtrip.prefab.json" );
    const sw::string binPath  = test::makeTempPath( "roundtrip.prefab.bin" );

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
    const sw::string srcXmlPath = sw::ensureSamplePrefabXml();
    sw::PrefabAsset  src;
    SW_ASSERT_TRUE( src.loadFromXmlFile( srcXmlPath ) );

    const sw::string xmlPath  = test::makeTempPath( "cachekey.prefab.xml" );
    const sw::string jsonPath = test::makeTempPath( "cachekey.prefab.json" );
    const sw::string binPath  = test::makeTempPath( "cachekey.prefab.bin" );
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
}

/**
 * @brief [PrefabTest] spawn 이 GameObject 를 만든다
 */
SW_TEST_CASE( PrefabTest, SpawnCreatesGameObject )
{
    const sw::string      srcXmlPath = sw::ensureSamplePrefabXml();
    sw::GameObjectManager objects;
    sw::PrefabManager     prefabs;
    sw::GameObject*       spawned = prefabs.spawn( &objects, srcXmlPath, "SpawnedSample" );
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

    const sw::string tempPath = test::makeTempPath( "in_memory_test.prefab.json" );
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( tempPath, prefabJson ) );

    sw::GameObjectManager objects;
    sw::PrefabManager     prefabs;

    sw::GameObject* spawned = prefabs.spawn( &objects, tempPath, "BossActor" );
    SW_ASSERT_NOT_NULL( spawned );
    SW_EXPECT_EQUAL( sw::string( "BossActor" ), sw::string( spawned->getName().c_str() ) );
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
    const sw::string tempPath   = test::makeTempPath( "circular_self.prefab.json" );
    const sw::string prefabJson = sw::string( R"({
		"_name": "CircularSelf",
		"_prefabAssetPath": ")" ) +
                                  tempPath + R"("
	})";

    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( tempPath, prefabJson ) );

    sw::GameObjectManager objects;
    sw::PrefabManager     prefabs;

    // 순환 참조 감지 시 무한 재귀 없이 안전하게 반환
    sw::GameObject* spawned = prefabs.spawn( &objects, tempPath, "TestCircular" );
    SW_ASSERT_NOT_NULL( spawned );
#endif
}

/**
 * @brief [PrefabTest] 프리팹을 다른 형식으로 저장해도(XML ↔ JSON) 컴포넌트가 남는다
 * @details 변환이 매니저 없는 `GameObject` 를 거쳤다 — 컴포넌트는 매니저가 이름으로 만드는데 그 팩토리가 없어, 변환한 본문에서 **컴포넌트가 모두
 *          빠졌다**(라운드트립 시험은 컴포넌트 없는 프리팹이라 잡지 못했다). 이제 쓰고 버리는 매니저 안에서 읽고 쓴다.
 */
SW_TEST_CASE( PrefabTest, ConvertingAPrefabKeepsItsComponents )
{
    const sw::PrefabAsset source   = sw::makeCratePrefab();
    const sw::string      jsonPath = test::makeTempPath( "crate.prefab.json" );
    const sw::string      xmlPath  = test::makeTempPath( "crate_back.prefab.xml" );
    SW_ASSERT_TRUE( source.saveToJsonFile( jsonPath ) );
    sw::PrefabAsset fromJson;
    SW_ASSERT_TRUE( fromJson.loadFromJsonFile( jsonPath ) );
    SW_EXPECT_TRUE( fromJson.getStateFormat() == sw::PrefabStateFormat::Json );
    SW_ASSERT_TRUE( fromJson.saveToXmlFile( xmlPath ) );
    sw::PrefabAsset fromXml;
    SW_ASSERT_TRUE( fromXml.loadFromXmlFile( xmlPath ) );

    sw::GameObjectManager check;
    for ( const sw::PrefabAsset* pAsset : { &fromJson, &fromXml } )
    {
        sw::GameObject* pObj = check.createGameObject( sw::hashed_string( "Check" ) );
        SW_ASSERT_TRUE( pAsset->applyStateTo( pObj ) );
        const sw::SceneComponent* pRoot = pObj->getPrimarySceneComponent();
        SW_ASSERT_NOT_NULL( pRoot );
        SW_EXPECT_TRUE( pRoot->getLocalPosition() == sw::float3( 1.0f, 2.0f, 3.0f ) );
    }
}

/**
 * @brief [PrefabTest] JSON 프리팹의 인스턴스를 되돌려도 컴포넌트가 남고 값이 되돌아간다
 * @details 되돌리기 두 자리(전체 · 오버라이드 하나)가 프리팹 본문을 XML 로만 읽었다. JSON 프리팹이면 컴포넌트를 모두 지운 뒤 읽기에 실패해
 *          **인스턴스가 비었다**(반환값은 무시됐다). 이제 프리팹이 읽을 때 정한 형식으로 읽는다(`PrefabAsset::applyStateTo`).
 */
SW_TEST_CASE( PrefabTest, JsonPrefabRevertKeepsComponents )
{
    const sw::string jsonPath = test::makeTempPath( "crate_revert.prefab.json" );
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToJsonFile( jsonPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( jsonPath, true ) );

    sw::GameObjectManager objects;
    sw::PrefabManager     prefabs;
    sw::GameObject*       pInstance = prefabs.spawn( &objects, jsonPath, "CrateA" );
    SW_ASSERT_NOT_NULL( pInstance );
    SW_ASSERT_NOT_NULL( pInstance->getPrimarySceneComponent() );
    pInstance->getPrimarySceneComponent()->setLocalScale( sw::float3( 5.0f, 5.0f, 5.0f ) );

    SW_ASSERT_TRUE( prefabs.revertInstance( pInstance, jsonPath ) );
    const sw::SceneComponent* pRoot = pInstance->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRoot );
    SW_EXPECT_TRUE( pRoot->getLocalScale() == sw::float3( 2.0f, 2.0f, 2.0f ) );
}

/**
 * @brief [PrefabTest] 되돌리기는 인스턴스의 자리를 지킨다 — 부모 · 이름 · 루트의 위치와 회전(스케일은 되돌린다)
 * @details 상태를 통째로 읽어 넣어, 다른 오브젝트에 붙어 있던 인스턴스가 루트로 떨어지고(프리팹 루트에는 부착이 없다) 이름과 자리가 프리팹의 것이
 *          됐다. 유니티 `PrefabUtility.RevertPrefabInstance` 는 루트의 위치 · 회전을 늘 인스턴스의 것으로 둔다.
 */
SW_TEST_CASE( PrefabTest, RevertKeepsTheInstancesPlaceAndParent )
{
    const sw::string xmlPath = test::makeTempPath( "crate_place.prefab.xml" );
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToXmlFile( xmlPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( xmlPath, false ) );

    sw::GameObjectManager objects;
    sw::PrefabManager     prefabs;
    sw::GameObject*       pTruck = objects.createGameObject( sw::hashed_string( "Truck" ) );
    SW_ASSERT_NOT_NULL( pTruck->addComponent<sw::SceneComponent>() );
    sw::GameObject* pInstance = prefabs.spawn( &objects, xmlPath, "CrateA" );
    SW_ASSERT_NOT_NULL( pInstance );
    SW_ASSERT_TRUE( pInstance->attachToParent( pTruck ) );
    pInstance->setName( sw::hashed_string( "CargoCrate" ) );
    sw::SceneComponent* pRoot = pInstance->getPrimarySceneComponent();
    pRoot->setLocalPosition( sw::float3( 9.0f, 8.0f, 7.0f ) );
    pRoot->setLocalRotation( sw::float3( 0.0f, 1.0f, 0.0f ) );
    pRoot->setLocalScale( sw::float3( 5.0f, 5.0f, 5.0f ) );

    SW_ASSERT_TRUE( prefabs.revertInstance( pInstance, xmlPath ) );
    pRoot = pInstance->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRoot );
    SW_EXPECT_TRUE( pInstance->getParent() == pTruck );
    SW_EXPECT_TRUE( pInstance->getName() == sw::hashed_string( "CargoCrate" ) );
    SW_EXPECT_TRUE( pRoot->getLocalPosition() == sw::float3( 9.0f, 8.0f, 7.0f ) );
    SW_EXPECT_TRUE( pRoot->getLocalRotation() == sw::float3( 0.0f, 1.0f, 0.0f ) );
    SW_EXPECT_TRUE( pRoot->getLocalScale() == sw::float3( 2.0f, 2.0f, 2.0f ) );
}

/**
 * @brief [PrefabTest] 엔진이 저작 프리팹(XML · JSON, 하위 폴더 포함)을 쿠킹본(.prefab.bin)으로 굽는다 — 읽지 못한 것 · 같은 쿠킹본을 쓰는 둘은 실패로 센다
 * @details 예전에는 파이썬(`CookAssets.py`)이 PFB2 형식을 따로 들고 `.prefab.xml` 만 구웠다 — `.prefab.json` 은 Shipping 에서 쿠킹본이 없어 스폰이
 *          실패했고, 엔진의 `cookPrefabToBinary` 는 쓰이지 않았다. 씬처럼 엔진이 굽는다(`App --cook-scenes` 가 함께 부른다, 언리얼 쿡 커맨드렛 자리).
 */
SW_TEST_CASE( PrefabTest, EngineCooksXmlAndJsonPrefabs )
{
    const sw::string sourceRoot = test::makeTempDirectory( "prefab_src" );
    const sw::string cookedRoot = test::makeTempDirectory( "prefab_cooked" );
    SW_ASSERT_FALSE( sourceRoot.empty() );
    SW_ASSERT_FALSE( cookedRoot.empty() );

    const sw::PrefabAsset crate = sw::makeCratePrefab();
    SW_ASSERT_TRUE( crate.saveToXmlFile( sourceRoot + "/crate.prefab.xml" ) );
    SW_ASSERT_TRUE( crate.saveToJsonFile( sourceRoot + "/barrel.prefab.json" ) );
    SW_ASSERT_TRUE( crate.saveToXmlFile( sourceRoot + "/sub/deep.prefab.xml" ) );
    // 읽지 못하는 프리팹과, 같은 쿠킹본(.prefab.bin)을 쓰는 두 소스.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourceRoot + "/broken.prefab.json", "{ not json" ) );
    SW_ASSERT_TRUE( crate.saveToXmlFile( sourceRoot + "/twin.prefab.xml" ) );
    SW_ASSERT_TRUE( crate.saveToJsonFile( sourceRoot + "/twin.prefab.json" ) );
    // 프리팹이 아닌 XML 은 건드리지 않는다.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourceRoot + "/notes.xml", "<Notes/>" ) );

    uint32       failedCount  = 0;
    const uint32 writtenCount = sw::PrefabManager::cookAllPrefabs( sourceRoot, cookedRoot, failedCount );
    SW_EXPECT_EQUAL( 4u, writtenCount ); // crate · barrel · deep · twin 둘 중 하나
    SW_EXPECT_EQUAL( 2u, failedCount );  // broken · twin 의 나머지 하나
    SW_EXPECT_FALSE( sw::FileUtil::fileExists( cookedRoot + "/broken.prefab.bin" ) );
    SW_EXPECT_FALSE( sw::FileUtil::fileExists( cookedRoot + "/notes.bin" ) );

    sw::GameObjectManager check;
    for ( const utf8* pCooked : { "/crate.prefab.bin", "/barrel.prefab.bin", "/sub/deep.prefab.bin" } )
    {
        sw::PrefabAsset cooked;
        SW_ASSERT_TRUE_MSG( cooked.loadFromBinaryFile( cookedRoot + pCooked ), pCooked );
        SW_EXPECT_TRUE( cooked.getName() == crate.getName() );
        sw::GameObject* pObj = check.createGameObject( sw::hashed_string( "Check" ) );
        SW_ASSERT_TRUE( cooked.applyStateTo( pObj ) );
        const sw::SceneComponent* pRoot = pObj->getPrimarySceneComponent();
        SW_ASSERT_NOT_NULL( pRoot );
        SW_EXPECT_TRUE( pRoot->getLocalPosition() == sw::float3( 1.0f, 2.0f, 3.0f ) );
    }
}
