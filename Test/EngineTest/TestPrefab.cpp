#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetFormat.h"

#include "EngineTest/TestGameObjectMocks.h"

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
                (void)sw::FileUtil::writeTextFile( path, pXmlContent ); // 못 쓰면 뒤의 로드가 실패로 드러난다
            }

            // 쿠킹본(.prefab.bin)도 같이 만들어 둔다. PrefabCache::getOrLoad 는 Shipping 에서
            // **바이너리만** 읽으므로(XML 은 Dev 전용 경로다), XML 만 두면 스폰 검증이 배포 빌드에서
            // 그냥 죽는다. 쿠커가 하는 일과 같은 변환을 테스트가 자기 손으로 해 둔다.
            sw::string binPath = path;
            binPath.replace( binPath.size() - 4, 4, ".bin" );
            if ( sw::FileUtil::fileExists( binPath ) == false )
            {
                sw::PrefabAsset cooked;
                if ( cooked.loadFromXmlFile( path ) )
                    (void)cooked.saveToBinaryFile( binPath ); // 못 쓰면 Shipping 의 로드가 실패로 드러난다
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
            return cooked.saveToBinaryFile( sw::AssetCookPath::toCookedPath( sourcePath ) ); // 런타임 로더 · 쿠커와 같은 이름 규칙
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
 * @brief [PrefabTest] 소스를 읽는 구성(Dev)은 소스가 없을 때 옆에 남은 쿠킹본을 대신 읽지 않는다 — 배포 구성은 쿠킹본만 읽는다
 * @details 소스를 옮기거나 지웠는데 쿠킹본(.prefab.bin)이 남아 있으면, Dev 가 그것을 읽어 낡은 프리팹이 살아나고 "없음" 실패가 가려진다.
 */
SW_TEST_CASE( PrefabTest, MissingSourceDoesNotFallBackToCookedBinaryInDev )
{
    const sw::string srcXmlPath = sw::ensureSamplePrefabXml();
    sw::PrefabAsset  src;
    SW_ASSERT_TRUE( src.loadFromXmlFile( srcXmlPath ) );

    // 소스는 없고 쿠킹본만 있는 프리팹.
    const sw::string orphanSourcePath = test::makeTempPath( "orphan.prefab.xml" );
    SW_ASSERT_TRUE( src.saveToBinaryFile( sw::AssetCookPath::toCookedPath( orphanSourcePath ) ) );
    SW_ASSERT_FALSE( sw::FileUtil::fileExists( orphanSourcePath ) );

    sw::PrefabCache  manager;
    sw::PrefabAsset* pLoaded{ nullptr };
    {
        SW_TEST_DEFENSIVE_SCOPE( "prefab whose source is gone" );
        pLoaded = manager.loadPrefab( orphanSourcePath );
    }
#if defined( SW_SHIPPING )
    SW_EXPECT_NOT_NULL( pLoaded );
#else
    SW_EXPECT_NULL( pLoaded );
#endif
}

/**
 * @brief [PrefabTest] JSON 으로 옮길 수 없는 상태는 JSON 파일을 쓰지 않고 실패한다 — 로더가 읽지 못하는 래퍼를 남기지 않는다
 * @details 상태를 JSON 으로 바꾸지 못할 때 다른 모양(`{formatVersion, name, xmlBody}` 같은 래퍼)으로 싸서 쓰고 성공을 돌려주면, 그 모양을 읽는 쪽이 없다.
 */
SW_TEST_CASE( PrefabTest, JsonSaveOfUnconvertibleStateWritesNothing )
{
    const sw::string      jsonPath = test::makeTempPath( "unconvertible.prefab.json" );
    const sw::PrefabAsset emptyPrefab;
    {
        SW_TEST_DEFENSIVE_SCOPE( "prefab state that cannot be converted to JSON" );
        SW_EXPECT_FALSE( emptyPrefab.saveToJsonFile( jsonPath ) );
    }
    SW_EXPECT_FALSE( sw::FileUtil::fileExists( jsonPath ) );
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

    sw::PrefabCache  manager;
    sw::PrefabAsset* fromXml  = manager.loadPrefab( xmlPath );
    sw::PrefabAsset* fromJson = manager.loadPrefab( jsonPath );
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
    sw::PrefabCache       prefabs;
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
    sw::PrefabCache       prefabs;

    sw::GameObject* spawned = prefabs.spawn( &objects, tempPath, "BossActor" );
    SW_ASSERT_NOT_NULL( spawned );
    SW_EXPECT_EQUAL( sw::string( "BossActor" ), sw::string( spawned->getName().c_str() ) );
#endif
}

/**
 * @brief [PrefabTest] 스폰이 같은 프리팹의 스폰을 부르면(순환) 안쪽 스폰만 거절되고 바깥 스폰은 끝난다 — 스택을 넘기지 않는다
 * @details 상태를 다 읽으면(`onPostLoad`) 같은 프리팹을 스폰하는 컴포넌트를 프리팹 안에 둔다 — 엔진에서 스폰이 스폰을 부르는 경로는 이것(컴포넌트
 *          수명 훅)뿐이다. 오브젝트의 프로퍼티가 아닌 칸으로 순환을 꾸미면 그 칸은 읽을 때 버려져 순환이 **한 번도** 일어나지 않고, 순환 방어를
 *          지워도 시험이 통과한다.
 */
SW_TEST_CASE( PrefabTest, CircularReferenceSpawnProtection )
{
    sw::GameObjectManager authoring;
    sw::RegisterMockComponents();
    sw::GameObject* pSource = authoring.createGameObject( sw::hashed_string( "CircularSelf" ) );
    SW_ASSERT_NOT_NULL( pSource->addComponent<sw::MockPostLoadSpawnerComponent>() );
    sw::PrefabAsset asset;
    asset.setFromGameObject( pSource );
    const sw::string xmlPath = test::makeTempPath( "circular_self.prefab.xml" );
    SW_ASSERT_TRUE( asset.saveToXmlFile( xmlPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( xmlPath, false ) );

    sw::GameObjectManager objects;
    sw::RegisterMockComponents();
    sw::PrefabCache prefabs;
    sw::MockPostLoadSpawnerComponent::s_pPrefabs          = &prefabs;
    sw::MockPostLoadSpawnerComponent::s_spawnPath         = xmlPath;
    sw::MockPostLoadSpawnerComponent::s_spawnAttemptCount = 0;
    sw::MockPostLoadSpawnerComponent::s_spawnedCount      = 0;
    sw::GameObject* pSpawned                              = nullptr;
    {
        test::ScopedDefensiveTestLog expected( "a prefab whose component spawns the same prefab" );
        pSpawned = prefabs.spawn( &objects, xmlPath, "TestCircular" );
    }
    sw::MockPostLoadSpawnerComponent::s_pPrefabs = nullptr;

    SW_ASSERT_NOT_NULL( pSpawned );                                              // 바깥 스폰은 끝난다
    SW_EXPECT_EQUAL( 1, sw::MockPostLoadSpawnerComponent::s_spawnAttemptCount ); // 안쪽 스폰은 한 번 시도되어
    SW_EXPECT_EQUAL( 0, sw::MockPostLoadSpawnerComponent::s_spawnedCount );      // 거절됐다
    SW_EXPECT_NOT_NULL( pSpawned->getComponent<sw::MockPostLoadSpawnerComponent>() );
}

/**
 * @brief [PrefabTest] 프리팹을 다른 형식으로 저장해도(XML ↔ JSON) 컴포넌트가 남는다
 * @details 변환은 쓰고 버리는 매니저 안에서 읽고 쓴다. 매니저 없는 `GameObject` 를 거치면 컴포넌트를 이름으로 만드는 팩토리가 없어 변환한 본문에서
 *          **컴포넌트가 모두 빠진다**(컴포넌트 없는 프리팹으로 하는 라운드트립 시험은 이것을 못 잡는다).
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
 * @brief [PrefabTest] 틱 안에서 스폰한 프리팹도 프리팹의 값을 가진다 — 상태는 틱 직후에 채운다
 * @details 오브젝트는 바로 돌려주고 상태는 틱 직후 구조 변경 큐에서 채운다. 틱 안에서 상태 읽기를 그 자리에서 돌리면 컴포넌트 추가가 모두
 *          미뤄져(nullptr) **프리팹의 값이 버려지고**, 틱 뒤에 생긴 컴포넌트는 기본값이다(스케일 1).
 */
SW_TEST_CASE( PrefabTest, SpawnDuringTickKeepsThePrefabsState )
{
    const sw::string xmlPath = test::makeTempPath( "crate_tick.prefab.xml" );
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToXmlFile( xmlPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( xmlPath, false ) );

    sw::GameObjectManager objects;
    sw::RegisterMockComponents();
    sw::PrefabCache        prefabs;
    sw::GameObject*        pSpawner = objects.createGameObject( sw::hashed_string( "Spawner" ) );
    sw::MockMeshComponent* pMock    = pSpawner->addComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( pMock );
    pMock->_pTickPrefabs      = &prefabs;
    pMock->_pTickSpawnManager = &objects;
    pMock->_tickSpawnPath     = xmlPath;

    objects.tick( 0.016f );

    SW_ASSERT_NOT_NULL( pMock->_pTickSpawned );
    const sw::SceneComponent* pRoot = pMock->_pTickSpawned->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRoot );
    SW_EXPECT_TRUE( pRoot->getLocalScale() == sw::float3( 2.0f, 2.0f, 2.0f ) );
    SW_EXPECT_STREQ( "TickSpawned", pMock->_pTickSpawned->getName().c_str() );
}

/**
 * @brief [PrefabTest] JSON 프리팹의 인스턴스를 되돌려도 컴포넌트가 남고 값이 되돌아간다
 * @details 되돌리기 두 자리(전체 · 오버라이드 하나)는 프리팹이 읽을 때 정한 형식으로 읽는다(`PrefabAsset::applyStateTo`). XML 로만 읽으면 JSON
 *          프리팹에서 컴포넌트를 모두 지운 뒤 읽기에 실패해 **인스턴스가 빈다**.
 */
SW_TEST_CASE( PrefabTest, JsonPrefabRevertKeepsComponents )
{
    const sw::string jsonPath = test::makeTempPath( "crate_revert.prefab.json" );
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToJsonFile( jsonPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( jsonPath, true ) );

    sw::GameObjectManager objects;
    sw::PrefabCache       prefabs;
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
 * @details 상태를 통째로 읽어 넣으면 다른 오브젝트에 붙어 있던 인스턴스가 루트로 떨어지고(프리팹 루트에는 부착이 없다) 이름과 자리가 프리팹의 것이
 *          된다. 유니티 `PrefabUtility.RevertPrefabInstance` 는 루트의 위치 · 회전을 늘 인스턴스의 것으로 둔다.
 */
SW_TEST_CASE( PrefabTest, RevertKeepsTheInstancesPlaceAndParent )
{
    const sw::string xmlPath = test::makeTempPath( "crate_place.prefab.xml" );
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToXmlFile( xmlPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( xmlPath, false ) );

    sw::GameObjectManager objects;
    sw::PrefabCache       prefabs;
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
 * @brief [PrefabTest] 프리팹 저장은 경로가 정한 형식으로 쓰고, 프리팹이 아닌 경로에는 쓰지 않는다
 * @details 쓰는 경로는 `PrefabAsset::saveToFile` 하나다. 늘 XML 로 쓰면 `.prefab.json` 에 XML 이 들어가 그 프리팹이 다시 읽히지 않고,
 *          경로를 확인하지 않으면 콘텐츠 브라우저에서 마지막에 클릭한 에셋(씬 · 머티리얼)을 프리팹으로 덮는다.
 */
SW_TEST_CASE( PrefabTest, SaveToFileWritesThePathsFormatAndRefusesOtherPaths )
{
    const sw::PrefabAsset crate    = sw::makeCratePrefab();
    const sw::string      jsonPath = test::makeTempPath( "crate_save.prefab.json" );
    const sw::string      xmlPath  = test::makeTempPath( "crate_save.prefab.xml" );
    SW_ASSERT_TRUE( crate.saveToFile( jsonPath ) );
    SW_ASSERT_TRUE( crate.saveToFile( xmlPath ) );
    sw::PrefabAsset fromJson;
    sw::PrefabAsset fromXml;
    SW_EXPECT_TRUE( fromJson.loadFromJsonFile( jsonPath ) );
    SW_EXPECT_TRUE( fromXml.loadFromXmlFile( xmlPath ) );

    const sw::string scenePath = test::makeTempPath( "level.scene.xml" );
    const sw::string kScene    = "<Scene name=\"Level\"/>\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( scenePath, kScene ) );
    {
        test::ScopedDefensiveTestLog expected( "a scene path is not a prefab path" );
        SW_EXPECT_FALSE( crate.saveToFile( scenePath ) );
    }
    sw::string sceneAfter;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( scenePath, sceneAfter ) );
    SW_EXPECT_STREQ( kScene.c_str(), sceneAfter.c_str() );
}

/**
 * @brief [PrefabTest] 되돌리기는 소켓에 단 인스턴스를 **그 소켓**에 둔다 — 컴포넌트 id 도 그대로라 핸들이 이어진다
 * @details 되돌리기가 부모를 오브젝트로 적고 `attachToParent`(부모의 primary)로 다시 붙이면 트럭 짐칸(소켓)에 실린 상자가 트럭 몸통으로 옮겨 가며
 *          자리가 튄다. 컴포넌트가 새 id 를 받으면 인스턴스의 컴포넌트를 가리키던 핸들(씬의 활성 카메라 · 게임 코드)이 끊긴다. 되돌리기 · 플레이
 *          종료 · 핫 리로드와 같이 원래 id 를 되살린다.
 */
SW_TEST_CASE( PrefabTest, RevertKeepsTheSocketAndTheComponentIds )
{
    const sw::string xmlPath = test::makeTempPath( "crate_socket.prefab.xml" );
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToXmlFile( xmlPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( xmlPath, false ) );

    sw::GameObjectManager objects;
    sw::PrefabCache       prefabs;
    sw::GameObject*       pTruck = objects.createGameObject( sw::hashed_string( "Truck" ) );
    sw::SceneComponent*   pBody  = pTruck->addComponent<sw::SceneComponent>();
    sw::SceneComponent*   pBed   = pTruck->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pBed->attachToComponent( pBody ) );
    sw::GameObject* pInstance = prefabs.spawn( &objects, xmlPath, "Cargo" );
    SW_ASSERT_NOT_NULL( pInstance );
    sw::SceneComponent* pRoot = pInstance->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRoot );
    SW_ASSERT_TRUE( pRoot->attachToComponent( pBed ) );
    const sw::ComponentHandle rootHandle = pRoot->getHandle();

    SW_ASSERT_TRUE( prefabs.revertInstance( pInstance, xmlPath ) );
    pRoot = pInstance->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRoot );
    SW_EXPECT_TRUE( pRoot->getParent() == pBed ); // 몸통이 아니라 짐칸
    SW_EXPECT_TRUE( objects.resolveComponent( rootHandle ) == pRoot );
}

/**
 * @brief [PrefabTest] 자식 인스턴스로 만든 프리팹에는 옛 부모가 실리지 않는다 — 스폰한 인스턴스는 같은 이름의 오브젝트에 붙지 않는다
 * @details 오브젝트 상태는 부모를 부착 필드로 든다. 그대로 실으면 플레이어 밑의 총으로 만든 프리팹에 "Player" 가 실려, 스폰할 때마다 그 씬에서
 *          이름이 "Player" 인 오브젝트에 옛 오프셋으로 붙는다. 프리팹 루트에는 부모가 없다 — 쓸 때 싣지 않는다. 부모가 실린 프리팹 파일은 프리팹을
 *          쓰는 쪽이 만들지 않는 모양이라, 읽을 때 붙이지 않고 경고로 알린다.
 */
SW_TEST_CASE( PrefabTest, PrefabMadeFromAChildDoesNotRememberItsParent )
{
    sw::GameObjectManager authoring;
    sw::GameObject*       pPlayer = authoring.createGameObject( sw::hashed_string( "Player" ) );
    sw::GameObject*       pGun    = authoring.createGameObject( sw::hashed_string( "Gun" ) );
    SW_ASSERT_NOT_NULL( pPlayer->addComponent<sw::SceneComponent>() );
    SW_ASSERT_NOT_NULL( pGun->addComponent<sw::SceneComponent>() );
    SW_ASSERT_TRUE( pGun->attachToParent( pPlayer ) );

    sw::PrefabAsset asset;
    asset.setFromGameObject( pGun );
    SW_ASSERT_TRUE( asset.isValid() );
    SW_EXPECT_TRUE( asset.getStateData().find( "Player" ) == sw::string::npos );
    const sw::string xmlPath = test::makeTempPath( "gun_from_child.prefab.xml" );
    SW_ASSERT_TRUE( asset.saveToXmlFile( xmlPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( xmlPath, false ) );

    // 부모를 실은 프리팹 파일(프리팹을 쓰는 쪽은 만들지 않는다) — 붙이지 않고 경고한다.
    const sw::string parentedPath = test::makeTempPath( "gun_parented.prefab.xml" );
    const sw::string parentedText = sw::string( "<Prefab formatVersion=\"0\" name=\"Gun\">" ) + sw::ObjectStateSerializer::saveToXmlString( pGun ) + "</Prefab>";
    SW_ASSERT_TRUE( parentedText.find( "Player" ) != sw::string::npos );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( parentedPath, parentedText ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( parentedPath, false ) );

    sw::GameObjectManager world;
    sw::GameObject*       pWorldPlayer = world.createGameObject( sw::hashed_string( "Player" ) );
    SW_ASSERT_NOT_NULL( pWorldPlayer->addComponent<sw::SceneComponent>() );
    sw::PrefabCache prefabs;
    for ( const sw::string& path : { xmlPath, parentedPath } )
    {
        uint32                   parentWarningCount = 0;
        const sw::DelegateHandle handle             = sw::Logger::addGlobalListener(
            SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [&parentWarningCount]( const sw::LogEntry& entry )
                    {
            if ( entry._level == sw::LogLevel::Warning && entry._message.find( "a prefab root has no parent" ) != sw::string::npos )
                ++parentWarningCount;
        } ) );
        sw::GameObject* pInstance       = nullptr;
        sw::GameObject* pBesideOriginal = nullptr;
        {
            SW_TEST_DEFENSIVE_SCOPE( "a prefab file that carries a parent reference" );
            pInstance = prefabs.spawn( &world, path, "Pickup" );
            // 원래 부모가 살아 있는 매니저(프리팹을 만든 씬에 바로 놓는 경우)에서도 붙지 않는다 — 이 프리팹에는 그 부모의 id 까지 실려 있다.
            pBesideOriginal = prefabs.spawn( &authoring, path, "PickupBesideOriginal" );
        }
        sw::Logger::removeGlobalListener( handle );
        SW_ASSERT_NOT_NULL( pInstance );
        SW_ASSERT_NOT_NULL( pBesideOriginal );
        SW_EXPECT_TRUE_MSG( pInstance->getParent() == nullptr, path.c_str() );
        SW_EXPECT_TRUE_MSG( pBesideOriginal->getParent() == nullptr, path.c_str() );
        // 프리팹을 쓰는 쪽이 만든 파일은 조용하고, 부모가 실린 파일은 스폰마다 알린다.
        SW_EXPECT_TRUE_MSG( parentWarningCount == ( path == parentedPath ? 2u : 0u ), path.c_str() );
    }
}

/**
 * @brief [PrefabTest] 엔진이 저작 프리팹(XML · JSON, 하위 폴더 포함)을 쿠킹본(.prefab.bin)으로 굽는다 — 읽지 못한 것 · 같은 쿠킹본을 쓰는 둘은 실패로 센다
 * @details 씬처럼 엔진이 굽는다(`App --cook-scenes` 가 함께 부른다, 언리얼 쿡 커맨드렛 자리). 형식을 따로 드는 외부 쿠커는 한 형식만 굽기 쉽다 —
 *          `.prefab.json` 이 빠지면 Shipping 에서 쿠킹본이 없어 스폰이 실패한다.
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
    const uint32 writtenCount = sw::PrefabCache::cookAllPrefabs( sourceRoot, cookedRoot, failedCount );
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

/**
 * @brief [PrefabTest] 컴포넌트 이름표는 프리팹의 세 형식(XML · JSON · 쿠킹한 PFB2)을 건너 남는다
 * @details 이름표는 오브젝트 안에서 컴포넌트를 가리키는 키다(부착 대상 · 프리팹 오버라이드). 프리팹이 이름표를 잃으면 스폰한 인스턴스가 기본값(타입 이름)을
 *          달고 나와, 그 이름으로 적힌 오버라이드 · 부착이 가리킬 곳을 잃는다.
 */
SW_TEST_CASE( PrefabTest, ComponentNameSurvivesEveryPrefabFormat )
{
    sw::GameObjectManager authoring;
    sw::GameObject*       pSource = authoring.createGameObject( sw::hashed_string( "Rifle" ) );
    SW_ASSERT_NOT_NULL( pSource->addComponent<sw::SceneComponent>() );
    sw::SceneComponent* pGrip = pSource->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pGrip );
    pGrip->setComponentName( sw::hashed_string( "Grip" ) );
    sw::PrefabAsset source;
    source.setFromGameObject( pSource );

    const sw::string xmlPath  = test::makeTempPath( "named.prefab.xml" );
    const sw::string jsonPath = test::makeTempPath( "named.prefab.json" );
    SW_ASSERT_TRUE( source.saveToXmlFile( xmlPath ) );
    SW_ASSERT_TRUE( source.saveToJsonFile( jsonPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( jsonPath, true ) );

    sw::PrefabAsset fromXml;
    sw::PrefabAsset fromJson;
    sw::PrefabAsset fromCooked;
    SW_ASSERT_TRUE( fromXml.loadFromXmlFile( xmlPath ) );
    SW_ASSERT_TRUE( fromJson.loadFromJsonFile( jsonPath ) );
    SW_ASSERT_TRUE( fromCooked.loadFromBinaryFile( sw::AssetCookPath::toCookedPath( jsonPath ) ) );

    sw::GameObjectManager check;
    for ( const sw::PrefabAsset* pAsset : { &fromXml, &fromJson, &fromCooked } )
    {
        sw::GameObject* pSpawned = check.createGameObject( sw::hashed_string( "Spawned" ) );
        SW_ASSERT_TRUE( pAsset->applyStateTo( pSpawned ) );
        SW_ASSERT_EQUAL( size_t( 2 ), pSpawned->getComponents().size() );
        SW_EXPECT_STREQ( "SceneComponent", pSpawned->getComponents()[0]->getComponentName().c_str() );
        SW_EXPECT_STREQ( "Grip", pSpawned->getComponents()[1]->getComponentName().c_str() );
    }
}

/**
 * @brief [PrefabTest] XML 프리팹은 저장하는 모양(`<Prefab name=…><GameObject/></Prefab>`) 하나만 읽는다 — 루트가 바로 `<GameObject>` 인 문서 ·
 *        `<GameObject>` 가 없는 `<Prefab>` 은 거절하고, 이름은 `name` 속성에서만 읽는다
 */
SW_TEST_CASE( PrefabTest, XmlPrefabReadsOnlyTheSavedShape )
{
    const sw::string barePath = test::makeTempPath( "bare.prefab.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( barePath, "<GameObject _name=\"Bare\"/>\n" ) );
    const sw::string emptyPath = test::makeTempPath( "empty.prefab.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( emptyPath, "<Prefab name=\"Empty\"/>\n" ) );
    const sw::string childNamePath = test::makeTempPath( "childname.prefab.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( childNamePath, "<Prefab><name>FromChild</name><GameObject _name=\"Crate\"/></Prefab>\n" ) );

    {
        SW_TEST_DEFENSIVE_SCOPE( "a prefab file that is not in the saved shape" );
        sw::PrefabAsset bare;
        SW_EXPECT_FALSE( bare.loadFromXmlFile( barePath ) );
        sw::PrefabAsset empty;
        SW_EXPECT_FALSE( empty.loadFromXmlFile( emptyPath ) );
    }

    sw::PrefabAsset childName;
    SW_ASSERT_TRUE( childName.loadFromXmlFile( childNamePath ) );
    SW_EXPECT_STREQ( "childname.prefab", childName.getName().c_str() ); // 이름 속성이 없으면 파일 이름이다
}
