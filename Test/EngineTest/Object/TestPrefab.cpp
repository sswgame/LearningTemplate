#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Resource/AssetFormat.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief 테스트용 샘플 XML 프리팹 파일을 이 케이스의 임시 경로에 만들고 경로를 반환합니다. */
        sw::string ensureSamplePrefabXML()
        {
            const sw::string path = test::makeTempPath( "sample_source.prefab.xml" );
            if ( sw::FileUtil::exists( path ) == false )
            {
                const utf8* pXMLContent = R"(<?xml version="1.0" encoding="utf-8"?>
<Prefab version="1" name="SampleHero">
    <GameObject _name="SampleHero">
    </GameObject>
</Prefab>)";
                (void)sw::FileUtil::writeTextFile( path, pXMLContent ); // 못 쓰면 뒤의 로드가 실패로 드러난다
            }

            // 쿠킹본(.prefab.bin)도 같이 만들어 둔다. PrefabCache::getOrLoad 는 Shipping 에서
            // **바이너리만** 읽으므로(XML 은 Dev 전용 경로다), XML 만 두면 스폰 검증이 배포 빌드에서
            // 그냥 죽는다. 쿠커가 하는 일과 같은 변환을 테스트가 자기 손으로 해 둔다.
            sw::string binPath = path;
            binPath.replace( binPath.size() - 4, 4, ".bin" );
            if ( sw::FileUtil::exists( binPath ) == false )
            {
                sw::PrefabAsset cooked;
                if ( cooked.loadFromXMLFile( path ) )
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
        [[nodiscard]] bool writeCookedBeside( const sw::string& sourcePath, bool bJSON )
        {
            sw::PrefabAsset cooked;
            if ( ( bJSON ? cooked.loadFromJSONFile( sourcePath ) : cooked.loadFromXMLFile( sourcePath ) ) == false )
                return false;
            return cooked.saveToBinaryFile( sw::AssetCookPath::toCookedPath( sourcePath ) ); // 런타임 로더 · 쿠커와 같은 이름 규칙
        }

        /** @brief 바퀴 프리팹: 씬 컴포넌트 하나(스케일 @p scale). */
        sw::PrefabAsset makeWheelPrefab( float32 scale )
        {
            sw::GameObjectManager authoring;
            sw::GameObject*       pSource = authoring.createGameObject( sw::hashed_string( "Wheel" ) );
            sw::SceneComponent*   pRoot   = pSource->addComponent<sw::SceneComponent>();
            pRoot->setLocalScale( sw::float3( scale, scale, scale ) );
            sw::PrefabAsset asset;
            asset.setFromGameObject( pSource );
            return asset;
        }

        /** @brief 바퀴의 루트를 (1,0,0) 으로 옮기는 오버라이드입니다(바퀴 프리팹의 원형과 다른 점). */
        constexpr const utf8* kWheelMovedOverride =
            "<PrefabOverrides><Override key=\"SceneComponent#0\"><SceneComponent _localPosition=\"1,0,0\" /></Override></PrefabOverrides>";

        /**
         * @brief 수레 프리팹: 루트와 루트에 붙은 "Axle" 씬 컴포넌트, 그리고 바퀴 프리팹의 중첩 인스턴스 둘.
         * @details "WheelL" 은 Axle 에 붙고 위치를 (1,0,0) 으로 덮어쓴다. "WheelR" 은 루트(primary)에 원본 그대로 붙는다.
         */
        sw::PrefabAsset makeCartPrefab( const sw::string& wheelPath )
        {
            sw::GameObjectManager authoring;
            sw::GameObject*       pSource = authoring.createGameObject( sw::hashed_string( "Cart" ) );
            sw::SceneComponent*   pRoot   = pSource->addComponent<sw::SceneComponent>();
            sw::SceneComponent*   pAxle   = pSource->addComponent<sw::SceneComponent>();
            pAxle->setComponentName( sw::hashed_string( "Axle" ) );
            (void)pAxle->attachToComponent( pRoot ); // 붙지 않으면 아래 부착 검사가 드러낸다
            sw::PrefabAsset asset;
            asset.setFromGameObject( pSource );

            sw::vector<sw::PrefabInstanceNode> listNode( 2 );
            listNode[0]._name        = "WheelL";
            listNode[0]._prefabPath  = wheelPath;
            listNode[0]._attachKey   = "Axle#0";
            listNode[0]._overrideXML = kWheelMovedOverride;
            listNode[1]._name        = "WheelR";
            listNode[1]._prefabPath  = wheelPath;
            (void)asset.setNestedInstances( std::move( listNode ) ); // 목록 규칙을 지킨다 — 어기면 아래 스폰이 드러낸다
            return asset;
        }

        /** @brief 프리팹을 저작 파일과 그 쿠킹본(Shipping 은 쿠킹본만 읽는다)으로 씁니다. */
        [[nodiscard]] bool writeWithCooked( const sw::PrefabAsset& asset, const sw::string& xmlPath )
        {
            return asset.saveToXMLFile( xmlPath ) && writeCookedBeside( xmlPath, false );
        }

        /** @brief @p pParent 의 자식 오브젝트 가운데 이름이 @p pName 으로 시작하는 첫 오브젝트입니다(매니저가 같은 이름에 번호를 붙인다). */
        sw::GameObject* findChildByNamePrefix( const sw::GameObject* pParent, const utf8* pName )
        {
            sw::vector<sw::GameObject*> listChild;
            pParent->getChildren( listChild );
            for ( sw::GameObject* pChild : listChild )
            {
                if ( sw::StringUtil::startsWith( pChild->getName().c_str(), pName, false ) )
                    return pChild;
            }
            return nullptr;
        }

        /**
         * @brief 노드 둘이 같은 내용이면 true 입니다. GUID 는 데이터베이스가 정하므로 보지 않습니다.
         * @details 오버라이드는 XML 을 다시 쓰며 공백이 바뀔 수 있어 글 전체가 아니라 덮어쓴 값(바퀴 위치)이 실렸는지만 견준다.
         */
        bool isSameNode( const sw::PrefabInstanceNode& left, const sw::PrefabInstanceNode& right )
        {
            const bool bLeftMoved  = left._overrideXML.find( "_localPosition=\"1,0,0\"" ) != sw::string::npos;
            const bool bRightMoved = right._overrideXML.find( "_localPosition=\"1,0,0\"" ) != sw::string::npos;
            return left._name == right._name && left._prefabPath == right._prefabPath && left._attachKey == right._attachKey &&
                   left._parentIndex == right._parentIndex && left._resolvedStateXML == right._resolvedStateXML && bLeftMoved == bRightMoved;
        }
    } // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) PrefabTest — 라운드트립·캐시 키·스폰
// ------------------------------------------------------------------------------
/**
 * @brief [PrefabTest] XML 로드 후 JSON/binary 라운드트립
 */
SW_TEST_CASE( PrefabTest, XMLAndJSONBinaryRoundtrip )
{
    const sw::string srcXMLPath = sw::ensureSamplePrefabXML();
    sw::PrefabAsset  src;
    SW_ASSERT_TRUE( src.loadFromXMLFile( srcXMLPath ) );
    SW_EXPECT_TRUE( src.isValid() );
    SW_EXPECT_FALSE( src.getStateData().empty() );

    const sw::string xmlPath  = test::makeTempPath( "roundtrip.prefab.xml" );
    const sw::string jsonPath = test::makeTempPath( "roundtrip.prefab.json" );
    const sw::string binPath  = test::makeTempPath( "roundtrip.prefab.bin" );

    SW_EXPECT_TRUE( src.saveToXMLFile( xmlPath ) );
    SW_EXPECT_TRUE( src.saveToJSONFile( jsonPath ) );
    SW_EXPECT_TRUE( src.saveToBinaryFile( binPath ) );

    sw::PrefabAsset fromXML;
    SW_EXPECT_TRUE( fromXML.loadFromXMLFile( xmlPath ) );
    SW_EXPECT_TRUE( fromXML.isValid() );
    SW_EXPECT_EQUAL( src.getName(), fromXML.getName() );

    sw::PrefabAsset fromJSON;
    SW_EXPECT_TRUE( fromJSON.loadFromJSONFile( jsonPath ) );
    SW_EXPECT_TRUE( fromJSON.isValid() );
    SW_EXPECT_EQUAL( src.getName(), fromJSON.getName() );

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
    const sw::string srcXMLPath = sw::ensureSamplePrefabXML();
    sw::PrefabAsset  src;
    SW_ASSERT_TRUE( src.loadFromXMLFile( srcXMLPath ) );

    // 소스는 없고 쿠킹본만 있는 프리팹.
    const sw::string orphanSourcePath = test::makeTempPath( "orphan.prefab.xml" );
    SW_ASSERT_TRUE( src.saveToBinaryFile( sw::AssetCookPath::toCookedPath( orphanSourcePath ) ) );
    SW_ASSERT_FALSE( sw::FileUtil::exists( orphanSourcePath ) );

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
SW_TEST_CASE( PrefabTest, JSONSaveOfUnconvertibleStateWritesNothing )
{
    const sw::string      jsonPath = test::makeTempPath( "unconvertible.prefab.json" );
    const sw::PrefabAsset emptyPrefab;
    {
        SW_TEST_DEFENSIVE_SCOPE( "prefab state that cannot be converted to JSON" );
        SW_EXPECT_FALSE( emptyPrefab.saveToJSONFile( jsonPath ) );
    }
    SW_EXPECT_FALSE( sw::FileUtil::exists( jsonPath ) );
}

/**
 * @brief [PrefabTest] 경로 구분자·확장자가 달라도 같은 캐시 엔트리
 */
SW_TEST_CASE( PrefabTest, CacheKeyNormalizesPathAndExtension )
{
    const sw::string srcXMLPath = sw::ensureSamplePrefabXML();
    sw::PrefabAsset  src;
    SW_ASSERT_TRUE( src.loadFromXMLFile( srcXMLPath ) );

    const sw::string xmlPath  = test::makeTempPath( "cachekey.prefab.xml" );
    const sw::string jsonPath = test::makeTempPath( "cachekey.prefab.json" );
    const sw::string binPath  = test::makeTempPath( "cachekey.prefab.bin" );
    SW_EXPECT_TRUE( src.saveToXMLFile( xmlPath ) );
    SW_EXPECT_TRUE( src.saveToJSONFile( jsonPath ) );
    SW_EXPECT_TRUE( src.saveToBinaryFile( binPath ) );

    sw::PrefabCache  manager;
    sw::PrefabAsset* fromXML  = manager.loadPrefab( xmlPath );
    sw::PrefabAsset* fromJSON = manager.loadPrefab( jsonPath );
    SW_ASSERT_NOT_NULL( fromXML );
    SW_EXPECT_EQUAL( fromXML, fromJSON );

    sw::string slashFlipped = xmlPath;
    for ( utf8& ch : slashFlipped )
    {
        if ( ch == '/' )
            ch = '\\';
        else if ( ch == '\\' )
            ch = '/';
    }
    if ( slashFlipped != xmlPath )
        SW_EXPECT_EQUAL( fromXML, manager.loadPrefab( slashFlipped ) );
}

/**
 * @brief [PrefabTest] spawn 이 GameObject 를 만든다
 */
SW_TEST_CASE( PrefabTest, SpawnCreatesGameObject )
{
    const sw::string      srcXMLPath = sw::ensureSamplePrefabXML();
    sw::GameObjectManager objects;
    sw::PrefabCache       prefabs;
    sw::GameObject*       spawned = prefabs.spawn( &objects, srcXMLPath, "SpawnedSample" );
    SW_ASSERT_NOT_NULL( spawned );
    SW_EXPECT_EQUAL( sw::string( "SpawnedSample" ), sw::string( spawned->getName().c_str() ) );
}

/**
 * @brief [PrefabTest] 인메모리 JSON 프리팹 에셋 생성, 파일 저장 및 스폰 검증
 */
SW_TEST_CASE( PrefabTest, InMemoryJSONPrefabCreationAndSpawn )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "InMemory JSON prefab spawn is Dev-only (Shipping requires cooked .bin)" );
#else
    const utf8* prefabJSON = R"({
		"_name": "DynamicPrefabActor"
	})";

    const sw::string tempPath = test::makeTempPath( "in_memory_test.prefab.json" );
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( tempPath, prefabJSON ) );

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
 *          지워도 테스트가 통과한다.
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
    SW_ASSERT_TRUE( asset.saveToXMLFile( xmlPath ) );
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
 *          **컴포넌트가 모두 빠진다**(컴포넌트 없는 프리팹으로 하는 라운드트립 테스트는 이것을 못 잡는다).
 */
SW_TEST_CASE( PrefabTest, ConvertingAPrefabKeepsItsComponents )
{
    const sw::PrefabAsset source   = sw::makeCratePrefab();
    const sw::string      jsonPath = test::makeTempPath( "crate.prefab.json" );
    const sw::string      xmlPath  = test::makeTempPath( "crate_back.prefab.xml" );
    SW_ASSERT_TRUE( source.saveToJSONFile( jsonPath ) );
    sw::PrefabAsset fromJSON;
    SW_ASSERT_TRUE( fromJSON.loadFromJSONFile( jsonPath ) );
    SW_EXPECT_TRUE( fromJSON.getStateFormat() == sw::PrefabStateFormat::JSON );
    SW_ASSERT_TRUE( fromJSON.saveToXMLFile( xmlPath ) );
    sw::PrefabAsset fromXML;
    SW_ASSERT_TRUE( fromXML.loadFromXMLFile( xmlPath ) );

    sw::GameObjectManager check;
    for ( const sw::PrefabAsset* pAsset : { &fromJSON, &fromXML } )
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
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToXMLFile( xmlPath ) );
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
SW_TEST_CASE( PrefabTest, JSONPrefabRevertKeepsComponents )
{
    const sw::string jsonPath = test::makeTempPath( "crate_revert.prefab.json" );
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToJSONFile( jsonPath ) );
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
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToXMLFile( xmlPath ) );
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
    sw::PrefabAsset fromJSON;
    sw::PrefabAsset fromXML;
    SW_EXPECT_TRUE( fromJSON.loadFromJSONFile( jsonPath ) );
    SW_EXPECT_TRUE( fromXML.loadFromXMLFile( xmlPath ) );

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
 *          종료 · 핫 리로드와 같이 원래 id 를 복원한다.
 */
SW_TEST_CASE( PrefabTest, RevertKeepsTheSocketAndTheComponentIDs )
{
    const sw::string xmlPath = test::makeTempPath( "crate_socket.prefab.xml" );
    SW_ASSERT_TRUE( sw::makeCratePrefab().saveToXMLFile( xmlPath ) );
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
    SW_ASSERT_TRUE( asset.saveToXMLFile( xmlPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( xmlPath, false ) );

    // 부모를 실은 프리팹 파일(프리팹을 쓰는 쪽은 만들지 않는다) — 붙이지 않고 경고한다.
    const sw::string parentedPath = test::makeTempPath( "gun_parented.prefab.xml" );
    const sw::string parentedText = sw::string( "<Prefab formatVersion=\"0\" name=\"Gun\">" ) + sw::ObjectStateSerializer::saveToXMLString( pGun ) + "</Prefab>";
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
        const sw::DelegateHandle handle             = sw::Logger::registerGlobalListener(
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
        sw::Logger::unregisterGlobalListener( handle );
        SW_ASSERT_NOT_NULL( pInstance );
        SW_ASSERT_NOT_NULL( pBesideOriginal );
        SW_EXPECT_TRUE_MSG( pInstance->getParent() == nullptr, path.c_str() );
        SW_EXPECT_TRUE_MSG( pBesideOriginal->getParent() == nullptr, path.c_str() );
        // 프리팹을 쓰는 쪽이 만든 파일은 조용하고, 부모가 실린 파일은 스폰마다 알린다.
        SW_EXPECT_TRUE_MSG( parentWarningCount == ( path == parentedPath ? 2u : 0u ), path.c_str() );
    }
}

/**
 * @brief [PrefabTest] 엔진이 저작 프리팹(XML · JSON, 하위 폴더 포함)을 쿠킹본(.prefab.bin)으로 쿠킹한다 — 읽지 못한 것 · 같은 쿠킹본을 쓰는 둘은 실패로 센다
 * @details 씬처럼 엔진이 쿠킹한다(`App --cook-scenes` 가 함께 부른다, 언리얼 쿡 커맨드렛 자리). 형식을 따로 드는 외부 쿠커는 한 형식만 쿠킹 쉽다 —
 *          `.prefab.json` 이 빠지면 Shipping 에서 쿠킹본이 없어 스폰이 실패한다.
 */
SW_TEST_CASE( PrefabTest, EngineCooksXMLAndJSONPrefabs )
{
    const sw::string sourceRoot = test::makeTempDirectory( "prefab_src" );
    const sw::string cookedRoot = test::makeTempDirectory( "prefab_cooked" );
    SW_ASSERT_FALSE( sourceRoot.empty() );
    SW_ASSERT_FALSE( cookedRoot.empty() );

    const sw::PrefabAsset crate = sw::makeCratePrefab();
    SW_ASSERT_TRUE( crate.saveToXMLFile( sourceRoot + "/crate.prefab.xml" ) );
    SW_ASSERT_TRUE( crate.saveToJSONFile( sourceRoot + "/barrel.prefab.json" ) );
    SW_ASSERT_TRUE( crate.saveToXMLFile( sourceRoot + "/sub/deep.prefab.xml" ) );
    // 읽지 못하는 프리팹과, 같은 쿠킹본(.prefab.bin)을 쓰는 두 소스.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourceRoot + "/broken.prefab.json", "{ not json" ) );
    SW_ASSERT_TRUE( crate.saveToXMLFile( sourceRoot + "/twin.prefab.xml" ) );
    SW_ASSERT_TRUE( crate.saveToJSONFile( sourceRoot + "/twin.prefab.json" ) );
    // 프리팹이 아닌 XML 은 건드리지 않는다.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourceRoot + "/notes.xml", "<Notes/>" ) );

    uint32       failedCount  = 0;
    const uint32 writtenCount = sw::PrefabCache::cookAllPrefabs( sourceRoot, cookedRoot, failedCount );
    SW_EXPECT_EQUAL( 4u, writtenCount ); // crate · barrel · deep · twin 둘 중 하나
    SW_EXPECT_EQUAL( 2u, failedCount );  // broken · twin 의 나머지 하나
    SW_EXPECT_FALSE( sw::FileUtil::exists( cookedRoot + "/broken.prefab.bin" ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( cookedRoot + "/notes.bin" ) );

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
 * @brief [PrefabTest] 쿠커가 만든 쿠킹본으로 읽은 프리팹은 그 소스(XML · JSON)로 읽은 프리팹과 같다
 * @details 개발 구성은 소스만, 배포 구성은 쿠킹본만 읽는다(`PrefabCache::loadPrefab`). 둘이 갈라지면 Shipping 에서만 다른 프리팹이 스폰되고
 *          개발 빌드의 테스트는 그것을 보지 못한다. 이름 · 위치 몇 개가 아니라, 둘을 같은 형식(JSON)으로 다시 써서 글 전체를 견준다.
 */
SW_TEST_CASE( PrefabTest, CookedPrefabMatchesItsSource )
{
    const sw::string sourceRoot = test::makeTempDirectory( "prefab_equiv_src" );
    const sw::string cookedRoot = test::makeTempDirectory( "prefab_equiv_cooked" );
    SW_ASSERT_FALSE( sourceRoot.empty() );
    SW_ASSERT_FALSE( cookedRoot.empty() );

    const sw::PrefabAsset crate = sw::makeCratePrefab();
    SW_ASSERT_TRUE( crate.saveToXMLFile( sourceRoot + "/crate.prefab.xml" ) );
    SW_ASSERT_TRUE( crate.saveToJSONFile( sourceRoot + "/barrel.prefab.json" ) );

    uint32       failedCount  = 0;
    const uint32 writtenCount = sw::PrefabCache::cookAllPrefabs( sourceRoot, cookedRoot, failedCount );
    SW_EXPECT_EQUAL( 2u, writtenCount );
    SW_EXPECT_EQUAL( 0u, failedCount );

    struct SourceAndCooked
    {
        const utf8* _pSource;
        const utf8* _pCooked;
        bool        _bJSON;
    };
    for ( const SourceAndCooked& pair : {
              SourceAndCooked{  "/crate.prefab.xml",  "/crate.prefab.bin", false},
              SourceAndCooked{"/barrel.prefab.json", "/barrel.prefab.bin",  true}
    } )
    {
        sw::PrefabAsset  fromSource;
        sw::PrefabAsset  fromCooked;
        const sw::string sourcePath = sourceRoot + pair._pSource;
        SW_ASSERT_TRUE_MSG( pair._bJSON ? fromSource.loadFromJSONFile( sourcePath ) : fromSource.loadFromXMLFile( sourcePath ), pair._pSource );
        SW_ASSERT_TRUE_MSG( fromCooked.loadFromBinaryFile( cookedRoot + pair._pCooked ), pair._pCooked );
        SW_EXPECT_EQUAL( fromSource.getName(), fromCooked.getName() );

        // 같은 형식으로 다시 써서 글 전체를 견준다. 형식마다 다른 읽은 상태의 표현은 여기서 지워진다.
        const sw::string sourceJSONPath = test::makeTempPath( "equiv_from_source.prefab.json" );
        const sw::string cookedJSONPath = test::makeTempPath( "equiv_from_cooked.prefab.json" );
        SW_ASSERT_TRUE( fromSource.saveToJSONFile( sourceJSONPath ) );
        SW_ASSERT_TRUE( fromCooked.saveToJSONFile( cookedJSONPath ) );
        sw::string sourceJSON;
        sw::string cookedJSON;
        SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sourceJSONPath, sourceJSON ) );
        SW_ASSERT_TRUE( sw::FileUtil::readTextFile( cookedJSONPath, cookedJSON ) );
        SW_EXPECT_TRUE_MSG( sourceJSON == cookedJSON, pair._pSource );
    }
}

/**
 * @brief [PrefabTest] 컴포넌트 이름은 프리팹의 세 형식(XML · JSON · 쿠킹한 PFB2)을 건너 남는다
 * @details 이름은 오브젝트 안에서 컴포넌트를 가리키는 키다(부착 대상 · 프리팹 오버라이드). 프리팹이 이름을 잃으면 스폰한 인스턴스가 기본값(타입 이름)을
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
    SW_ASSERT_TRUE( source.saveToXMLFile( xmlPath ) );
    SW_ASSERT_TRUE( source.saveToJSONFile( jsonPath ) );
    SW_ASSERT_TRUE( sw::writeCookedBeside( jsonPath, true ) );

    sw::PrefabAsset fromXML;
    sw::PrefabAsset fromJSON;
    sw::PrefabAsset fromCooked;
    SW_ASSERT_TRUE( fromXML.loadFromXMLFile( xmlPath ) );
    SW_ASSERT_TRUE( fromJSON.loadFromJSONFile( jsonPath ) );
    SW_ASSERT_TRUE( fromCooked.loadFromBinaryFile( sw::AssetCookPath::toCookedPath( jsonPath ) ) );

    sw::GameObjectManager check;
    for ( const sw::PrefabAsset* pAsset : { &fromXML, &fromJSON, &fromCooked } )
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
SW_TEST_CASE( PrefabTest, XMLPrefabReadsOnlyTheSavedShape )
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
        SW_EXPECT_FALSE( bare.loadFromXMLFile( barePath ) );
        sw::PrefabAsset empty;
        SW_EXPECT_FALSE( empty.loadFromXMLFile( emptyPath ) );
    }

    sw::PrefabAsset childName;
    SW_ASSERT_TRUE( childName.loadFromXMLFile( childNamePath ) );
    SW_EXPECT_STREQ( "childname.prefab", childName.getName().c_str() ); // 이름 속성이 없으면 파일 이름이다
}

// ------------------------------------------------------------------------------
// 2) PrefabTest — 중첩 프리팹(프리팹 안의 다른 프리팹 인스턴스)
// ------------------------------------------------------------------------------
/**
 * @brief [PrefabTest] 중첩 인스턴스 노드(원본 경로 · 붙을 컴포넌트 · 오버라이드 · 부모 노드)는 세 형식(XML · JSON · PFB2)을 건너 남는다
 * @details 노드에 붙는 노드는 XML 에서 그 `<PrefabInstance>` 안에, JSON 에서 그 객체의 `_listPrefabInstance` 에 들어간다. 읽은 순서가 바뀌면 부모 인덱스가
 *          어긋나 다른 부품에 붙는다.
 */
SW_TEST_CASE( PrefabTest, NestedInstancesSurviveEveryPrefabFormat )
{
    const sw::string                   wheelPath = test::makeTempPath( "nest_fmt_wheel.prefab.xml" );
    sw::PrefabAsset                    cart      = sw::makeCartPrefab( wheelPath );
    sw::vector<sw::PrefabInstanceNode> listNode  = cart.getNestedInstances();
    sw::PrefabInstanceNode             hubcap{};
    hubcap._name        = "Hubcap";
    hubcap._prefabPath  = wheelPath;
    hubcap._parentIndex = 1; // WheelR 에 붙는다
    listNode.push_back( hubcap );
    SW_ASSERT_TRUE( cart.setNestedInstances( listNode ) );

    const sw::string xmlPath  = test::makeTempPath( "nest_fmt_cart.prefab.xml" );
    const sw::string jsonPath = test::makeTempPath( "nest_fmt_cart.prefab.json" );
    const sw::string binPath  = test::makeTempPath( "nest_fmt_cart.prefab.bin" );
    SW_ASSERT_TRUE( cart.saveToXMLFile( xmlPath ) );
    SW_ASSERT_TRUE( cart.saveToJSONFile( jsonPath ) );
    SW_ASSERT_TRUE( cart.saveToBinaryFile( binPath ) );

    sw::PrefabAsset fromXML;
    sw::PrefabAsset fromJSON;
    sw::PrefabAsset fromBinary;
    SW_ASSERT_TRUE( fromXML.loadFromXMLFile( xmlPath ) );
    SW_ASSERT_TRUE( fromJSON.loadFromJSONFile( jsonPath ) );
    SW_ASSERT_TRUE( fromBinary.loadFromBinaryFile( binPath ) );
    for ( const sw::PrefabAsset* pLoaded : { &fromXML, &fromJSON, &fromBinary } )
    {
        SW_ASSERT_EQUAL( size_t( 3 ), pLoaded->getNestedInstances().size() );
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            SW_EXPECT_TRUE_MSG( sw::isSameNode( listNode[index], pLoaded->getNestedInstances()[index] ), listNode[index]._name.c_str() );
        }
    }
    // JSON 의 상태 본문에는 노드 배열이 남지 않는다 — 오브젝트 상태로 읽는 쪽이 모르는 칸이다.
    SW_EXPECT_TRUE( fromJSON.getStateData().find( "_listPrefabInstance" ) == sw::string::npos );

    // 부모가 자기보다 뒤인 목록은 받지 않는다.
    sw::vector<sw::PrefabInstanceNode> listBroken( 1 );
    listBroken[0]._prefabPath  = wheelPath;
    listBroken[0]._parentIndex = 0;
    {
        SW_TEST_DEFENSIVE_SCOPE( "a nested instance whose parent does not come before it" );
        SW_EXPECT_FALSE( cart.setNestedInstances( listBroken ) );
    }
    SW_EXPECT_EQUAL( size_t( 3 ), cart.getNestedInstances().size() );
}

/**
 * @brief [PrefabTest] 중첩 인스턴스는 자식 오브젝트로 스폰되어 지정한 컴포넌트에 붙고 오버라이드를 가진다 — 원본을 고치면 상위를 다시 쓰지 않아도 퍼진다
 * @details 수레 프리팹은 바퀴 프리팹의 경로와 덮어쓴 위치만 가진다. 바퀴의 스케일을 고치면 다음에 스폰한 수레의 두 바퀴에 모두 퍼지고, 덮어쓴 위치는 남는다
 *          (유니티 Nested Prefab 과 같다).
 */
SW_TEST_CASE( PrefabTest, NestedInstancesSpawnAttachedWithTheirOverrides )
{
    const sw::string wheelPath = test::makeTempPath( "nest_spawn_wheel.prefab.xml" );
    const sw::string cartPath  = test::makeTempPath( "nest_spawn_cart.prefab.xml" );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeWheelPrefab( 2.0f ), wheelPath ) );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeCartPrefab( wheelPath ), cartPath ) );

    sw::GameObjectManager objects;
    sw::PrefabCache       prefabs;
    sw::GameObject*       pCart = prefabs.spawn( &objects, cartPath, "Cart" );
    SW_ASSERT_NOT_NULL( pCart );
    sw::GameObject* pLeft  = sw::findChildByNamePrefix( pCart, "WheelL" );
    sw::GameObject* pRight = sw::findChildByNamePrefix( pCart, "WheelR" );
    SW_ASSERT_TRUE( pLeft != nullptr && pRight != nullptr );
    SW_ASSERT_TRUE( pLeft->getPrimarySceneComponent() != nullptr && pRight->getPrimarySceneComponent() != nullptr );

    const sw::Component* pAxle = sw::ComponentStableKey::findComponent( pCart, "Axle#0" );
    SW_ASSERT_NOT_NULL( pAxle );
    SW_EXPECT_TRUE( pLeft->getPrimarySceneComponent()->getParent() == pAxle );
    SW_EXPECT_TRUE( pRight->getPrimarySceneComponent()->getParent() == pCart->getPrimarySceneComponent() );
    SW_EXPECT_TRUE( pLeft->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 1.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( pRight->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 0.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( pLeft->getPrimarySceneComponent()->getLocalScale() == sw::float3( 2.0f, 2.0f, 2.0f ) );

    const sw::PrefabNestedPart* pPart = objects.getSceneService<sw::PrefabInstanceRegistry>().findNestedPart( pLeft->getObjectID() );
    SW_ASSERT_NOT_NULL( pPart );
    SW_EXPECT_EQUAL( pCart->getObjectID(), pPart->_ownerObjectID );
    SW_EXPECT_TRUE( objects.getSceneService<sw::PrefabInstanceRegistry>().isAttachedNestedPart( pLeft ) );

    // 원본(바퀴)만 고친다 — 수레 파일은 그대로다.
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeWheelPrefab( 3.0f ), wheelPath ) );
    prefabs.reload( wheelPath );
    sw::GameObject* pCartAgain = prefabs.spawn( &objects, cartPath, "CartAgain" );
    SW_ASSERT_NOT_NULL( pCartAgain );
    sw::GameObject* pLeftAgain  = sw::findChildByNamePrefix( pCartAgain, "WheelL" );
    sw::GameObject* pRightAgain = sw::findChildByNamePrefix( pCartAgain, "WheelR" );
    SW_ASSERT_TRUE( pLeftAgain != nullptr && pRightAgain != nullptr );
    SW_EXPECT_TRUE( pLeftAgain->getPrimarySceneComponent()->getLocalScale() == sw::float3( 3.0f, 3.0f, 3.0f ) );
    SW_EXPECT_TRUE( pRightAgain->getPrimarySceneComponent()->getLocalScale() == sw::float3( 3.0f, 3.0f, 3.0f ) );
    SW_EXPECT_TRUE( pLeftAgain->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 1.0f, 0.0f, 0.0f ) ); // 덮어쓴 값은 남는다
}

/**
 * @brief [PrefabTest] 중첩 프리팹의 순환은 로드 오류다 — 순환에 든 프리팹은 로드되지 않고, 순환 밖에서 그것을 놓은 프리팹은 로드되어 그 노드만 건너뛴다
 * @details 컴포넌트가 로드 뒤 같은 프리팹을 스폰하는 순환(`CircularReferenceSpawnProtection`)은 스폰에서 막고, 데이터의 순환은 로드에서 막는다. 로드에서 막지
 *          않으면 스폰이 A → B → A 를 끝없이 펼친다.
 */
SW_TEST_CASE( PrefabTest, CircularNestedPrefabIsALoadError )
{
    const sw::string pathA     = test::makeTempPath( "nest_cycle_a.prefab.xml" );
    const sw::string pathB     = test::makeTempPath( "nest_cycle_b.prefab.xml" );
    const sw::string pathSelf  = test::makeTempPath( "nest_cycle_self.prefab.xml" );
    const sw::string pathOuter = test::makeTempPath( "nest_cycle_outer.prefab.xml" );
    // A 는 B 를, B 는 A 를 놓는다. Self 는 자기를 놓는다. Outer 는 A 를 놓지만 순환 밖이다.
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeCartPrefab( pathB ), pathA ) );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeCartPrefab( pathA ), pathB ) );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeCartPrefab( pathSelf ), pathSelf ) );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeCartPrefab( pathA ), pathOuter ) );

    sw::PrefabCache       prefabs;
    sw::GameObjectManager objects;
    sw::GameObject*       pOuter = nullptr;
    {
        SW_TEST_DEFENSIVE_SCOPE( "circular nested prefabs" );
        SW_EXPECT_NULL( prefabs.loadPrefab( pathA ) );
        SW_EXPECT_NULL( prefabs.loadPrefab( pathB ) );
        SW_EXPECT_NULL( prefabs.loadPrefab( pathSelf ) );
        SW_EXPECT_NOT_NULL( prefabs.loadPrefab( pathOuter ) );
        pOuter = prefabs.spawn( &objects, pathOuter, "Outer" );
    }
    SW_ASSERT_NOT_NULL( pOuter );
    SW_EXPECT_FALSE( pOuter->hasChildren() ); // 순환에 든 노드는 스폰하지 않는다
    SW_EXPECT_FALSE( prefabs.isCached( pathA ) );
}

/**
 * @brief [PrefabTest] 쿠킹은 중첩 인스턴스를 펼친다 — 쿠킹본은 원본 없이 부품을 스폰하고, 원본을 읽지 못한 프리팹은 쿠킹하지 않는다
 * @details 펼친 쿠킹본은 노드마다 오버라이드를 얹은 최종 상태와 원본의 중첩 인스턴스(여기서는 기차 → 수레 → 바퀴)를 싣는다. 배포본은 스폰할 때 원본 파일을 열지
 *          않는다. 원본이 없는 프리팹을 쿠킹본으로 쓰면 배포본이 부품 없이 스폰하므로 실패로 센다.
 */
SW_TEST_CASE( PrefabTest, CookFlattensNestedInstancesSoTheOriginalIsNotRead )
{
    const sw::string sourceRoot = test::makeTempDirectory( "nest_cook_src" );
    const sw::string cookedRoot = test::makeTempDirectory( "nest_cook_cooked" );
    SW_ASSERT_FALSE( sourceRoot.empty() );
    SW_ASSERT_FALSE( cookedRoot.empty() );

    const sw::string wheelPath = sourceRoot + "/wheel.prefab.xml";
    const sw::string cartPath  = sourceRoot + "/cart.prefab.xml";
    const sw::string trainPath = sourceRoot + "/train.prefab.xml";
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeWheelPrefab( 2.0f ), wheelPath ) );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeCartPrefab( wheelPath ), cartPath ) );
    sw::PrefabAsset                    train = sw::makeWheelPrefab( 1.0f );
    sw::vector<sw::PrefabInstanceNode> listCar( 1 );
    listCar[0]._name       = "Car";
    listCar[0]._prefabPath = cartPath;
    SW_ASSERT_TRUE( train.setNestedInstances( listCar ) );
    SW_ASSERT_TRUE( sw::writeWithCooked( train, trainPath ) );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeCartPrefab( sourceRoot + "/missing.prefab.xml" ), sourceRoot + "/broken_cart.prefab.xml" ) );

    uint32 failedCount  = 0;
    uint32 writtenCount = 0;
    {
        SW_TEST_DEFENSIVE_SCOPE( "a prefab whose nested prefab is missing" );
        writtenCount = sw::PrefabCache::cookAllPrefabs( sourceRoot, cookedRoot, failedCount );
    }
    SW_EXPECT_EQUAL( 3u, writtenCount ); // wheel · cart · train
    SW_EXPECT_EQUAL( 1u, failedCount );  // broken_cart
    SW_EXPECT_FALSE( sw::FileUtil::exists( cookedRoot + "/broken_cart.prefab.bin" ) );

    sw::PrefabAsset cookedTrain;
    SW_ASSERT_TRUE( cookedTrain.loadFromBinaryFile( cookedRoot + "/train.prefab.bin" ) );
    SW_ASSERT_EQUAL( size_t( 3 ), cookedTrain.getNestedInstances().size() ); // Car, 그 아래 WheelL · WheelR
    SW_EXPECT_TRUE( cookedTrain.hasResolvedNestedInstances() );
    SW_EXPECT_EQUAL( -1, cookedTrain.getNestedInstances()[0]._parentIndex );
    SW_EXPECT_EQUAL( 0, cookedTrain.getNestedInstances()[1]._parentIndex );
    SW_EXPECT_EQUAL( 0, cookedTrain.getNestedInstances()[2]._parentIndex );

    // 원본을 지운다 — 펼친 쿠킹본은 그것을 읽지 않는다.
    for ( const sw::string& path : { wheelPath, cartPath } )
    {
        SW_ASSERT_TRUE( sw::FileUtil::removeFile( path ) );
        SW_ASSERT_TRUE( sw::FileUtil::removeFile( sw::AssetCookPath::toCookedPath( path ) ) );
    }
    sw::GameObjectManager objects;
    sw::PrefabCache       prefabs;
    sw::GameObject*       pTrain = objects.createGameObject( sw::hashed_string( "Train" ) );
    SW_ASSERT_TRUE( cookedTrain.applyStateTo( pTrain ) );
    SW_EXPECT_EQUAL( 3u, prefabs.spawnNestedInstances( pTrain, cookedTrain ) );
    sw::GameObject* pCar = sw::findChildByNamePrefix( pTrain, "Car" );
    SW_ASSERT_NOT_NULL( pCar );
    sw::GameObject* pLeft = sw::findChildByNamePrefix( pCar, "WheelL" );
    SW_ASSERT_TRUE( pLeft != nullptr && pLeft->getPrimarySceneComponent() != nullptr );
    SW_EXPECT_TRUE( pLeft->getPrimarySceneComponent()->getParent() == sw::ComponentStableKey::findComponent( pCar, "Axle#0" ) );
    SW_EXPECT_TRUE( pLeft->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 1.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( pLeft->getPrimarySceneComponent()->getLocalScale() == sw::float3( 2.0f, 2.0f, 2.0f ) );

    // 펼친 프리팹은 저작 파일로 쓸 수 없다 — 원본 경로를 잃은 부품이 두 번 스폰된다.
    {
        SW_TEST_DEFENSIVE_SCOPE( "a cooked prefab written as a source file" );
        SW_EXPECT_FALSE( cookedTrain.saveToXMLFile( test::makeTempPath( "nest_cooked_back.prefab.xml" ) ) );
    }
}

/**
 * @brief [PrefabTest] 인스턴스로 프리팹을 다시 만들면(Apply to Prefab) 부품의 수정이 그 노드의 오버라이드가 되고, 붙은 자리도 남는다
 * @details 부품을 싣지 않으면 중첩 프리팹 인스턴스에서 Apply 할 때마다 파일에서 중첩이 사라진다.
 */
SW_TEST_CASE( PrefabTest, PrefabMadeFromAnInstanceKeepsItsNestedPartsAsOverrides )
{
    const sw::string wheelPath = test::makeTempPath( "nest_apply_wheel.prefab.xml" );
    const sw::string cartPath  = test::makeTempPath( "nest_apply_cart.prefab.xml" );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeWheelPrefab( 2.0f ), wheelPath ) );
    SW_ASSERT_TRUE( sw::writeWithCooked( sw::makeCartPrefab( wheelPath ), cartPath ) );

    sw::GameObjectManager objects;
    sw::PrefabCache       prefabs;
    sw::GameObject*       pCart = prefabs.spawn( &objects, cartPath, "Cart" );
    SW_ASSERT_NOT_NULL( pCart );
    sw::GameObject* pRight = sw::findChildByNamePrefix( pCart, "WheelR" );
    SW_ASSERT_TRUE( pRight != nullptr && pRight->getPrimarySceneComponent() != nullptr );
    pRight->getPrimarySceneComponent()->setLocalPosition( sw::float3( 0.0f, 0.0f, 5.0f ) );
    objects.flushSceneTransforms();

    sw::PrefabAsset applied;
    applied.setFromGameObject( pCart, &prefabs );
    const sw::vector<sw::PrefabInstanceNode>& listNode = applied.getNestedInstances();
    SW_ASSERT_EQUAL( size_t( 2 ), listNode.size() );
    SW_EXPECT_STREQ( "WheelL", listNode[0]._name.c_str() );
    SW_EXPECT_STREQ( "Axle#0", listNode[0]._attachKey.c_str() );
    SW_EXPECT_TRUE_MSG( listNode[0]._overrideXML.find( "_localPosition=\"1,0,0\"" ) != sw::string::npos, listNode[0]._overrideXML.c_str() );
    SW_EXPECT_STREQ( "WheelR", listNode[1]._name.c_str() );
    SW_EXPECT_TRUE( listNode[1]._attachKey.empty() );
    SW_EXPECT_TRUE_MSG( listNode[1]._overrideXML.find( "_localPosition=\"0,0,5\"" ) != sw::string::npos, listNode[1]._overrideXML.c_str() );
    SW_EXPECT_TRUE( listNode[0]._prefabPath == wheelPath && listNode[1]._prefabPath == wheelPath );

    // 다시 쓴 프리팹을 스폰하면 같은 자리다.
    const sw::string appliedPath = test::makeTempPath( "nest_apply_cart_applied.prefab.xml" );
    SW_ASSERT_TRUE( sw::writeWithCooked( applied, appliedPath ) );
    sw::GameObjectManager check;
    sw::GameObject*       pCheck = prefabs.spawn( &check, appliedPath, "Check" );
    SW_ASSERT_NOT_NULL( pCheck );
    sw::GameObject* pCheckRight = sw::findChildByNamePrefix( pCheck, "WheelR" );
    SW_ASSERT_TRUE( pCheckRight != nullptr && pCheckRight->getPrimarySceneComponent() != nullptr );
    SW_EXPECT_TRUE( pCheckRight->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 0.0f, 0.0f, 5.0f ) );
}

/**
 * @brief [PrefabTest] 프리로드할 프리팹 목록은 중첩 인스턴스의 원본을 한 번씩 담는다
 * @details 같은 바퀴를 두 번 놓은 수레의 목록에는 바퀴가 한 번 나온다. 프리로드 수집은 이 목록을 그대로 쓰고 목록의 프리팹마다 다시 부른다.
 */
SW_TEST_CASE( PrefabTest, ReferencedPrefabPathsListNestedSourcesOnce )
{
    const sw::string       wheelPath = test::makeTempPath( "nest_refs_wheel.prefab.xml" );
    const sw::PrefabAsset  cart      = sw::makeCartPrefab( wheelPath );
    sw::vector<sw::string> listPath;
    cart.collectReferencedPrefabPaths( listPath );
    SW_ASSERT_EQUAL( size_t( 1 ), listPath.size() );
    SW_EXPECT_STREQ( wheelPath.c_str(), listPath[0].c_str() );
}
