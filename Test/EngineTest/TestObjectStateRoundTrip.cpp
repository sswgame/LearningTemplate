#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 테스트 편의 — 자식 GameObject 목록을 값으로 (엔진 API 는 out 인자다). */
    sw::vector<sw::GameObject*> childrenOf( const sw::GameObject& gameObject )
    {
        sw::vector<sw::GameObject*> listChild;
        gameObject.getChildren( listChild );
        return listChild;
    }
} // namespace

using namespace sw;

/**
 * @brief [ObjectStateRoundTripTest] SceneComponent 트랜스폼이 XML 왕복에서 보존되는지 검증
 * @details 기준선 — 가장 파생 타입이 SceneComponent 인 경우다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, SceneComponentTransformSurvivesXml )
{
    GameObjectManager manager;
    GameObject*       pSource = manager.createGameObject( hashed_string( "Source" ) );
    SW_ASSERT_NOT_NULL( pSource );

    SceneComponent* pScene = pSource->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pScene );
    pScene->setLocalPosition( float3{ 1.0f, 2.0f, 3.0f } );

    const string xml = ObjectStateSerializer::saveToXmlString( pSource );
    SW_EXPECT_FALSE( xml.empty() );

    GameObject* pTarget = manager.createGameObject( hashed_string( "Target" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( ObjectStateSerializer::loadFromXmlString( pTarget, xml ) );

    SceneComponent* pLoaded = pTarget->getComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pLoaded );
    const float3 position = pLoaded->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( 1.0f, position._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, position._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, position._z, 1e-4f );
}

/**
 * @brief [ObjectStateRoundTripTest] 파생 컴포넌트가 상속한 트랜스폼도 XML 왕복에서 보존되는지 검증
 * @details `MeshComponent` 는 `SceneComponent` 를 상속하므로 위치는 상속된 PROPERTY 다.
 *          모든 직렬화기가 `TypeInfo::forEachProperty` 를 기본값(`bIncludeBase = false`)으로 부르면
 *          이 값이 저장도 되지 않고 로드도 되지 않는다 — 씬·프리팹·Undo 스냅샷이 전부 같은 경로다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, DerivedComponentInheritedTransformSurvivesXml )
{
    GameObjectManager manager;
    GameObject*       pSource = manager.createGameObject( hashed_string( "MeshSource" ) );
    SW_ASSERT_NOT_NULL( pSource );

    MeshComponent* pMesh = pSource->addComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setLocalPosition( float3{ -2.0f, 0.5f, 4.0f } );
    pMesh->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );

    const string xml = ObjectStateSerializer::saveToXmlString( pSource );
    SW_EXPECT_FALSE( xml.empty() );

    GameObject* pTarget = manager.createGameObject( hashed_string( "MeshTarget" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( ObjectStateSerializer::loadFromXmlString( pTarget, xml ) );

    MeshComponent* pLoaded = pTarget->getComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pLoaded );

    const float3 position = pLoaded->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( -2.0f, position._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, position._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, position._z, 1e-4f );

    const float3 scale = pLoaded->getLocalScale();
    SW_EXPECT_NEAR_EQUAL( 2.0f, scale._x, 1e-4f );
}

// ------------------------------------------------------------------------------
// 7) ObjectStateXmlSerializerTest — XML 저장·계층 라운드트립
// ------------------------------------------------------------------------------
/**
 * @brief [ObjectStateRoundTripTest] 메시의 블렌드 모드가 저장 · 로드를 지난다
 * @details `RHIBlendMode` 에 `ENUM()` 이 없어 직렬화기가 이름을 몰랐다 — 씬 · 프리팹에 `_blendMode="null"` 로 적혔고(저장소의 에셋 셋이 그랬다),
 *          읽을 때는 기본값(불투명)으로 돌아갔다. 반투명으로 바꾼 메시가 저장할 때마다 불투명이 됐다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, MeshBlendModeSurvivesXml )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pSource = manager.createGameObject( sw::hashed_string( "GlassPane" ) );
    sw::MeshComponent*    pMesh   = pSource->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setBlendMode( sw::RHIBlendMode::Transparent );

    const sw::string xml = sw::ObjectStateSerializer::saveToXmlString( pSource );
    SW_EXPECT_TRUE_MSG( xml.find( "_blendMode=\"Transparent\"" ) != sw::string::npos, xml.c_str() );

    sw::GameObject* pCopy = manager.createGameObject( sw::hashed_string( "GlassPaneCopy" ) );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pCopy, xml ) );
    const sw::MeshComponent* pCopyMesh = pCopy->getComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pCopyMesh );
    SW_EXPECT_TRUE( pCopyMesh->getBlendMode() == sw::RHIBlendMode::Transparent );
}

/**
 * @brief [ObjectStateRoundTripTest] 메시의 머티리얼 참조가 저장 · 로드를 지나고, 잡은 참조는 경로를 비우거나 컴포넌트가 사라질 때 놓인다
 * @details 메시의 머티리얼은 날 포인터(`_pMaterial`)뿐이라 저장되지 않았다 — 씬 · 프리팹을 다시 열면 모든 메시가 씬 기본 머티리얼이 됐다.
 *          언리얼 `UMeshComponent::OverrideMaterials` · 유니티 `Renderer.sharedMaterials` 는 에셋 참조로 저장된다. 스프라이트가 따로 들던
 *          `_materialName`(읽는 곳이 없었다)은 이 참조의 옛 이름으로 읽힌다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, MeshMaterialReferenceSurvivesXml )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    constexpr const utf8* kPath = "engine/materials/benchtextured.material";
    sw::MaterialCache&    cache = sw::engine::getResourceManager().getMaterialManager();
    SW_ASSERT_TRUE_MSG( cache.isCached( kPath ) == false, "다른 시험이 이 머티리얼을 잡고 있다 — 참조 검사가 비었다" );

    sw::GameObjectManager manager;
    sw::GameObject*       pSource = manager.createGameObject( sw::hashed_string( "Painted" ) );
    sw::MeshComponent*    pMesh   = pSource->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setMaterialPath( kPath );
    SW_EXPECT_TRUE( cache.isCached( kPath ) );
    SW_ASSERT_NOT_NULL( pMesh->getMaterial() );

    const sw::string xml = sw::ObjectStateSerializer::saveToXmlString( pSource );
    SW_EXPECT_TRUE_MSG( xml.find( "_materialPath=\"engine/materials/benchtextured.material\"" ) != sw::string::npos, xml.c_str() );

    sw::GameObject* pCopy = manager.createGameObject( sw::hashed_string( "PaintedCopy" ) );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pCopy, xml ) );
    sw::MeshComponent* pCopyMesh = pCopy->getComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pCopyMesh );
    SW_EXPECT_STREQ( kPath, pCopyMesh->getMaterialPath().c_str() );
    // 로드는 값만 채운다. 잡는 것은 시작(onBeginPlay) · 씬 초기화 · 프로퍼티 편집이다 — 같은 캐시의 머티리얼이 걸린다.
    pCopyMesh->resolveMaterialAsset();
    SW_EXPECT_TRUE( pCopyMesh->getMaterial() == pMesh->getMaterial() );

    // 경로를 비우면 놓고 씬 기본으로 돌아간다(포인터 없음). 사본이 아직 잡고 있다.
    pMesh->setMaterialPath( "" );
    SW_EXPECT_TRUE( pMesh->getMaterial() == nullptr );
    SW_EXPECT_TRUE( cache.isCached( kPath ) );
    // 사본이 사라지면 마지막 참조가 놓인다.
    manager.destroyObject( pCopy );
    manager.processDeferredDestruction();
    SW_EXPECT_FALSE( cache.isCached( kPath ) );

    // 스프라이트의 옛 칸 이름(`_materialName`)도 이 참조로 읽힌다.
    sw::GameObject* pSprite = manager.createGameObject( sw::hashed_string( "OldSprite" ) );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString(
        pSprite, "<GameObject _name=\"OldSprite\"><_listComponent><SpriteComponent _materialName=\"engine/materials/benchtextured.material\" />"
                 "</_listComponent></GameObject>" ) );
    const sw::SpriteComponent* pSpriteComp = pSprite->getComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pSpriteComp );
    SW_EXPECT_STREQ( kPath, pSpriteComp->getMaterialPath().c_str() );
}

/**
 * @brief [ObjectStateRoundTripTest] 플레이 중이 아니어도 상태를 읽은 메시는 그릴 메시 · 머티리얼을 갖는다 — 편집 중 되돌리기 · 프리팹 드래그
 * @details 상태를 읽으면 컴포넌트를 새로 만든다. 메시는 렌더 에셋(메시 id → 메시, 머티리얼 참조 → 머티리얼)을 시작(`onBeginPlay`) · 씬 초기화에서만
 *          풀어, **편집 중**에 되돌리기 · 프리팹 드래그로 다시 만든 메시는 다음 플레이 · 씬 재로드까지 그려지지 않았다(GpuScene 은 메시 없는 것을
 *          건너뛴다). 언리얼 `PostLoad` · 유니티 `OnAfterDeserialize` 처럼 상태를 읽은 뒤 컴포넌트마다 `onPostLoad` 를 부른다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, LoadedMeshResolvesItsRenderAssetsWithoutPlay )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager; // 시작하지 않은 월드(편집 중)
    sw::GameObject*       pSource = manager.createGameObject( sw::hashed_string( "Statue" ) );
    sw::MeshComponent*    pMesh   = pSource->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setMaterialPath( "engine/materials/benchtextured.material" );
    SW_ASSERT_NOT_NULL( pMesh->getRawMesh() );
    const sw::string xml = sw::ObjectStateSerializer::saveToXmlString( pSource );

    // 되돌리기 — 같은 오브젝트에 제자리로 다시 읽는다.
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pSource, xml ) );
    const sw::MeshComponent* pReloaded = pSource->getComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_EXPECT_TRUE( pReloaded->getRawMesh() != nullptr );
    SW_EXPECT_TRUE( pReloaded->getMaterial() != nullptr );

    // 프리팹 드래그 · 복제 — 새 오브젝트에 읽는다.
    sw::GameObject* pCopy = manager.createGameObject( sw::hashed_string( "StatueCopy" ) );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pCopy, xml ) );
    const sw::MeshComponent* pCopyMesh = pCopy->getComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pCopyMesh );
    SW_EXPECT_TRUE( pCopyMesh->getRawMesh() != nullptr );
}

/**
 * @brief [ObjectStateRoundTripTest] 부모를 제자리에서 다시 읽어도(되돌리기 · 프리팹으로 되돌리기 · 플레이 종료 복원) 다른 오브젝트의 자식이 붙어 있다
 * @details 제자리 로드는 컴포넌트를 모두 지우고 새로 만드는데, 씬 컴포넌트의 소멸자가 자식을 떼어 **다른 오브젝트의 자식들이 루트가
 *          됐다.** 로드는 이 오브젝트 안의 부착만 되붙였다. 에디터에서 부모의 속성 하나를 고치고 되돌리면 자식이 떨어져 월드 자리가 튀었다.
 *          XML · JSON · 바이너리 세 로더가 같은 길이다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, InPlaceReloadKeepsOtherObjectsChildren )
{
    for ( uint32 format = 0; format < 3; ++format )
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pParent     = manager.createGameObject( sw::hashed_string( "ReloadParent" ) );
        sw::GameObject*       pChild      = manager.createGameObject( sw::hashed_string( "ReloadChild" ) );
        sw::SceneComponent*   pRoot       = pParent->addComponent<sw::SceneComponent>();
        sw::SceneComponent*   pChildScene = pChild->addComponent<sw::SceneComponent>();
        SW_ASSERT_NOT_NULL( pRoot );
        SW_ASSERT_NOT_NULL( pChildScene );
        pRoot->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
        pRoot->setLocalRotation( sw::float3( 0.0f, 0.5f, 0.0f ) );
        pChildScene->setLocalPosition( sw::float3( 0.0f, 2.0f, 1.0f ) );
        SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
        manager.flushSceneTransforms();
        const sw::float3 worldBefore = pChildScene->getWorldPosition();

        const sw::ObjectIdentity identity = sw::ObjectStateSerializer::captureIdentity( pParent );
        bool                     bLoaded  = false;
        if ( format == 0 )
            bLoaded = sw::ObjectStateSerializer::loadFromXmlString( pParent, sw::ObjectStateSerializer::saveToXmlString( pParent ), { &identity } );
        else if ( format == 1 )
            bLoaded = sw::ObjectStateSerializer::loadFromJsonString( pParent, sw::ObjectStateSerializer::saveToJsonString( pParent ), { &identity } );
        else
        {
            sw::vector<uint8> buffer;
            SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pParent, buffer ) );
            bLoaded = sw::ObjectStateSerializer::loadFromBinaryBuffer( pParent, buffer.data(), buffer.size(), { &identity } ) != 0;
        }
        SW_ASSERT_TRUE( bLoaded );
        manager.flushSceneTransforms();

        SW_EXPECT_TRUE( pChild->getParent() == pParent );
        const sw::float3 worldAfter = pChildScene->getWorldPosition();
        SW_EXPECT_NEAR_EQUAL( worldBefore._x, worldAfter._x, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( worldBefore._y, worldAfter._y, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( worldBefore._z, worldAfter._z, 1e-3f );
    }
}

/**
 * @brief [ObjectStateRoundTripTest] 오브젝트 안의 부착(소켓)은 복사본 안에서 잇는다 — 이름이 바뀐 복사본이 원본에 붙지 않는다
 * @details 오브젝트 안의 부착도 소유자 이름을 적어, 원본이 살아 있는 매니저에 같은 상태를 읽으면(복제 · 같은 프리팹 두 번 · 영속 이월) 복사본의 이름이
 *          유일하게 바뀌어(`Rig` → `Rig_2`) 이름으로 **원본**을 찾았다 — 복사본의 팔이 원본의 루트에 붙었다. 이제 소유자 칸을 비운다(= 자기).
 *          옛 데이터(자기 이름을 적은 것)는 읽기 전 이름과 견준다. 세 형식이 같은 길이다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, AttachmentInsideAnObjectStaysInsideItsCopy )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pRig  = manager.createGameObject( sw::hashed_string( "Rig" ) );
    sw::SceneComponent*   pRoot = pRig->addComponent<sw::SceneComponent>();
    sw::SceneComponent*   pArm  = pRig->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pArm->attachToComponent( pRoot ) );

    const sw::string xml = sw::ObjectStateSerializer::saveToXmlString( pRig );
    SW_EXPECT_TRUE( xml.find( "_attachOwner=\"Rig\"" ) == sw::string::npos ); // 자기 이름을 적지 않는다

    const auto expectArmOnOwnRoot = []( sw::GameObject* pCopy, const utf8* pStep )
    {
        SW_ASSERT_TRUE_MSG( pCopy->getName() != sw::hashed_string( "Rig" ), pStep ); // 이름이 유일하게 바뀐 복사본이다
        sw::vector<sw::SceneComponent*> listScene;
        for ( sw::Component* pComp : pCopy->getComponents() )
        {
            if ( pComp != nullptr && pComp->isSceneComponent() )
                listScene.push_back( static_cast<sw::SceneComponent*>( pComp ) );
        }
        SW_ASSERT_TRUE_MSG( listScene.size() == 2, pStep );
        SW_EXPECT_TRUE_MSG( listScene[1]->getParent() == listScene[0], pStep );
    };

    BLOCK( "XML · JSON · 바이너리 — 원본이 살아 있는 매니저에 읽는다" )
    {
        sw::GameObject* pXmlCopy = manager.createGameObject( sw::hashed_string( "Rig" ) );
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pXmlCopy, xml ) );
        expectArmOnOwnRoot( pXmlCopy, "XML" );

        sw::GameObject* pJsonCopy = manager.createGameObject( sw::hashed_string( "Rig" ) );
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromJsonString( pJsonCopy, sw::ObjectStateSerializer::saveToJsonString( pRig ) ) );
        expectArmOnOwnRoot( pJsonCopy, "JSON" );

        sw::vector<uint8> bytes;
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pRig, bytes ) );
        sw::GameObject* pBinaryCopy = manager.createGameObject( sw::hashed_string( "Rig" ) );
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromBinaryBuffer( pBinaryCopy, bytes.data(), bytes.size() ) > 0 );
        expectArmOnOwnRoot( pBinaryCopy, "바이너리" );
    }

    BLOCK( "옛 데이터 — 자기 안의 부착에 자기 이름을 적었고 id 칸이 없다" )
    {
        // 빈 이름은 "None" 으로 적힌다(읽으면 빈 값). 옛 저장은 같은 오브젝트의 부모에도 소유자 이름을 적었다.
        const sw::string kIdField    = "_attachOwnerId=\"0\"";
        const sw::string kEmptyOwner = "_attachOwner=\"None\"";
        sw::string       legacy      = xml;
        SW_ASSERT_TRUE( legacy.find( kIdField ) != sw::string::npos );
        for ( size_t found = legacy.find( kIdField ); found != sw::string::npos; found = legacy.find( kIdField ) )
            legacy.erase( found, kIdField.size() );
        for ( size_t found = legacy.find( kEmptyOwner ); found != sw::string::npos; found = legacy.find( kEmptyOwner ) )
            legacy.replace( found, kEmptyOwner.size(), "_attachOwner=\"Rig\"" );
        SW_ASSERT_TRUE( legacy.find( "_attachOwner=\"Rig\"" ) != sw::string::npos );
        sw::GameObject* pLegacyCopy = manager.createGameObject( sw::hashed_string( "Rig" ) );
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pLegacyCopy, legacy ) );
        expectArmOnOwnRoot( pLegacyCopy, "옛 데이터" );
    }

    SW_EXPECT_TRUE( pArm->getParent() == pRoot ); // 원본은 그대로
    SW_EXPECT_EQUAL( size_t( 1 ), pRoot->getChildren().size() );
}

/**
 * @brief [ObjectStateXmlSerializerTest] XML 문자열 저장·로드
 */
SW_TEST_CASE( ObjectStateXmlSerializerTest, SaveAndLoadXmlString )
{
    sw::GameObjectManager manager;
    sw::GameObject*       sourcePtr = manager.createGameObject( sw::hashed_string( "SerializedHero" ) );
    sw::GameObject&       source    = *sourcePtr;
    source.setActive( false );

    const sw::string xml = ObjectStateSerializer::saveToXmlString( &source );
    SW_ASSERT_FALSE( xml.empty() );
    SW_EXPECT_TRUE( xml.find( "GameObject" ) != sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "SerializedHero" ) != sw::string::npos );
    // 컨테이너는 프로퍼티 이름 요소로 직접 나간다(<vector _name=..> 래핑 없음).
    // 비어 있으면 self-closing(<_listComponent />)이라 여는 태그만으로 찾는다.
    SW_EXPECT_TRUE( xml.find( "<_listComponent" ) != sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "_name=\"_listComponent\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "SceneTransforms" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "_parentGO" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "ParentGO" ) == sw::string::npos );

    const sw::string json = ObjectStateSerializer::saveToJsonString( &source );
    SW_ASSERT_FALSE( json.empty() );
    // 컨테이너는 프로퍼티 이름 아래 배열로 직접 나간다("vector"/"_name" 래핑 없음).
    SW_EXPECT_TRUE( json.find( "\"_listComponent\":[" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"vector\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"Components\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "ParentGO" ) == sw::string::npos );

    manager.clear();

    sw::GameObject* targetPtr = manager.createGameObject( sw::hashed_string( "Temp" ) );
    sw::GameObject& target    = *targetPtr;
    target.setActive( true );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( &target, xml ) );
    SW_EXPECT_STREQ( "SerializedHero", target.getName().c_str() );
    SW_EXPECT_FALSE( target.isActive() );

    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXmlString( nullptr, xml ) );
    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXmlString( &target, "" ) );
    SW_EXPECT_EMPTY( ObjectStateSerializer::saveToXmlString( nullptr ) );

    // JSON 도 로드까지 돌려 본다 — 두 포맷은 몸통 하나(직렬화기만 다르다)를 쓰고, 예전엔 JSON 로드를 지나는 테스트가 없었다.
    manager.clear();
    sw::GameObject* jsonTargetPtr = manager.createGameObject( sw::hashed_string( "TempJson" ) );
    SW_ASSERT_NOT_NULL( jsonTargetPtr );
    jsonTargetPtr->setActive( true );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromJsonString( jsonTargetPtr, json ) );
    SW_EXPECT_STREQ( "SerializedHero", jsonTargetPtr->getName().c_str() );
    SW_EXPECT_FALSE( jsonTargetPtr->isActive() );

    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromJsonString( nullptr, json ) );
    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromJsonString( jsonTargetPtr, "" ) );
    SW_EXPECT_EMPTY( ObjectStateSerializer::saveToJsonString( nullptr ) );
}

/**
 * @brief [ObjectStateXmlSerializerTest] 부모-자식 계층 라운드트립
 */
SW_TEST_CASE( ObjectStateXmlSerializerTest, ParentChildHierarchyRoundtrip )
{
    Scene* scene = engine::getSceneManager().getActiveScene();
    if ( scene == nullptr )
        scene = engine::getSceneManager().createScene( "GOHierarchySerializer" );
    SW_ASSERT_NOT_NULL( scene );
    SW_ASSERT_NOT_NULL( scene->getObjectManager() );

    GameObjectManager* manager = scene->getObjectManager();
    manager->clear();

    GameObject* parent = manager->createGameObject( hashed_string( "ParentGO" ) );
    GameObject* child  = manager->createGameObject( hashed_string( "ChildGO" ) );
    GameObject* grand  = manager->createGameObject( hashed_string( "GrandGO" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    parent->addComponent<sw::SceneComponent>();
    child->addComponent<sw::SceneComponent>();
    grand->addComponent<sw::SceneComponent>();

    parent->getComponent<sw::SceneComponent>()->setLocalPosition( sw::float3( 1.0f, 2.0f, 3.0f ) );
    child->getComponent<sw::SceneComponent>()->setLocalPosition( sw::float3( 4.0f, 5.0f, 6.0f ) );

    SW_ASSERT_TRUE( child->attachToParent( parent ) );
    SW_ASSERT_TRUE( grand->attachToParent( child ) );

    const sw::string parentXml = ObjectStateSerializer::saveToXmlString( parent );
    const sw::string childXml  = ObjectStateSerializer::saveToXmlString( child );
    const sw::string grandXml  = ObjectStateSerializer::saveToXmlString( grand );
    SW_ASSERT_FALSE( parentXml.empty() );
    SW_ASSERT_FALSE( childXml.empty() );
    SW_ASSERT_FALSE( grandXml.empty() );

    SW_EXPECT_TRUE( childXml.find( "ParentGO" ) != sw::string::npos );
    SW_EXPECT_TRUE( grandXml.find( "ChildGO" ) != sw::string::npos );
    const uint64 parentSavedId = parent->getObjectId();
    const uint64 childSavedId  = child->getObjectId();
    const uint64 grandSavedId  = grand->getObjectId();

    // 계층을 해체하고 빈 GO 를 다시 만든 뒤 한 묶음으로 읽는다(Play 스냅샷 순서). 새 오브젝트는 새 id 라, 묶음이 상태를 찍을 때의 id 로 서로를 찾는다.
    manager->clear();
    parent = manager->createGameObject( hashed_string( "TempParent" ) );
    child  = manager->createGameObject( hashed_string( "TempChild" ) );
    grand  = manager->createGameObject( hashed_string( "TempGrand" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    // 자식을 부모보다 먼저 로드한다 — 읽는 자리에서는 부모가 아직 없고, 묶음의 끝(`finish`)이 잇는다(비순서 스냅샷 복원).
    ObjectStateBatch batch( ObjectIdSpace::Saved );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( child, childXml, { nullptr, &batch, childSavedId } ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( grand, grandXml, { nullptr, &batch, grandSavedId } ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( parent, parentXml, { nullptr, &batch, parentSavedId } ) );
    SW_EXPECT_NULL( child->getParent() );
    batch.finish();

    parent = manager->findGameObjectByName( hashed_string( "ParentGO" ) );
    child  = manager->findGameObjectByName( hashed_string( "ChildGO" ) );
    grand  = manager->findGameObjectByName( hashed_string( "GrandGO" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    SW_EXPECT_NULL( parent->getParent() );
    SW_EXPECT_EQUAL( parent, child->getParent() );
    SW_EXPECT_EQUAL( child, grand->getParent() );
    SW_EXPECT_EQUAL( size_t( 1 ), childrenOf( *parent ).size() );
    SW_EXPECT_EQUAL( child, childrenOf( *parent )[0] );
    SW_EXPECT_EQUAL( size_t( 1 ), childrenOf( *child ).size() );
    SW_EXPECT_EQUAL( grand, childrenOf( *child )[0] );

    const sw::float3 parentPos = parent->getComponent<sw::SceneComponent>()->getLocalPosition();
    const sw::float3 childPos  = child->getComponent<sw::SceneComponent>()->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( 1.0f, parentPos._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, parentPos._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, parentPos._z, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, childPos._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, childPos._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, childPos._z, 0.0001f );

    manager->clear();
}
