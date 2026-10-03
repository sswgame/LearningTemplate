#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/MissingComponent.h"
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

    /**
     * @brief 상태 시험의 enum(`sw::StateShiftColor`)을 열거자 목록으로 레지스트리에 올립니다. 같은 이름으로 다시 부르면 그 자리에 덮어씁니다 — 다음 빌드 ·
     *        핫 리로드가 열거자를 옮기거나 지운 자리입니다(코드의 값은 그대로).
     */
    void registerStateShiftColor( const sw::vector<sw::pair<const utf8*, int64>>& listEnumerator )
    {
        sw::EnumInfo info;
        info._name               = sw::hashed_string( "StateShiftColor" );
        info._fullyQualifiedName = sw::hashed_string( "sw::StateShiftColor" );
        info._size               = static_cast<uint8>( sizeof( int32 ) );
        info._bIsSigned          = SW_TRUE;
        info._bIsBitFlag         = SW_FALSE;
        for ( const sw::pair<const utf8*, int64>& enumerator : listEnumerator )
        {
            const sw::hashed_string name( enumerator.first );
            info._mapNameToValue[name]              = enumerator.second;
            info._mapValueToName[enumerator.second] = name;
        }
        sw::engine::getTypeRegistry().registerEnum( info );
    }

    /** @brief 저장하는 빌드의 열거자 — 코드의 값과 같습니다. 시험은 끝에 이것으로 되돌립니다. */
    void registerStateShiftColorAsDeclared()
    {
        registerStateShiftColor( {
            {  "Red", 0},
            {"Green", 1},
            { "Blue", 2}
        } );
    }
} // namespace

namespace sw
{
    /**
     * @brief 저장한 뒤에 PROPERTY 하나(`_dropped`)가 사라지는 컴포넌트입니다 — 다음 빌드 · 핫 리로드의 자리. 코드젠 없이 타입 정보를 손으로 등록하고,
     *        시험이 같은 이름으로 다시 등록해(핫 리로드가 하는 일) 칸 하나를 지운다. 타입 정보는 레지스트리의 것을 쓴다(재등록이 같은 주소에 덮는다).
     */
    class SchemaShiftComponent : public Component
    {
    public:
        REFLECT_BODY();
        const TypeInfo* getTypeInfo() const override { return StaticType(); }

        int32 _kept{ 0 };
        int32 _dropped{ 0 };
    };

    const TypeInfo* SchemaShiftComponent::StaticType()
    {
        static const TypeInfo* s_pType = []()
        {
            TypeInfo info{};
            info._name               = hashed_string( "SchemaShiftComponent" );
            info._fullyQualifiedName = hashed_string( "sw::SchemaShiftComponent" );
            info._size               = sizeof( SchemaShiftComponent );
            info._addComponent       = &GameObject::addComponentTo<SchemaShiftComponent>;
            // 지울 칸이 **앞**이다 — 그 칸에서 읽기가 멈추면 뒤의 `_kept` 가 읽히지 않아 드러난다.
            info._listProperty = {
                {hashed_string( "_dropped" ), hashed_string( "int32" ), SW_OFFSET_OF( SchemaShiftComponent, _dropped )},
                {   hashed_string( "_kept" ), hashed_string( "int32" ), SW_OFFSET_OF( SchemaShiftComponent,    _kept )}
            };
            engine::getTypeRegistry().registerClass( info );
            return engine::getTypeRegistry().findType( hashed_string( "sw::SchemaShiftComponent" ) );
        }();
        return s_pType;
    }

    /** @brief 상태 시험의 enum 입니다. 레지스트리의 열거자 표는 시험이 손으로 올린다(`registerStateShiftColor`). */
    enum class StateShiftColor : int32
    {
        Red   = 0,
        Green = 1,
        Blue  = 2,
    };

    /**
     * @brief enum 칸 하나와 그 **뒤의** 칸 하나를 가진 컴포넌트입니다 — enum 칸을 읽지 못해도 뒤의 칸 · 뒤의 컴포넌트가 읽히는지 본다. 타입 정보는 손으로
     *        등록한다(`SchemaShiftComponent` 와 같은 모양).
     */
    class EnumShiftComponent : public Component
    {
    public:
        REFLECT_BODY();
        const TypeInfo* getTypeInfo() const override { return StaticType(); }

        StateShiftColor _color{ StateShiftColor::Red };
        int32           _after{ 0 };
    };

    const TypeInfo* EnumShiftComponent::StaticType()
    {
        static const TypeInfo* s_pType = []()
        {
            TypeInfo info{};
            info._name               = hashed_string( "EnumShiftComponent" );
            info._fullyQualifiedName = hashed_string( "sw::EnumShiftComponent" );
            info._size               = sizeof( EnumShiftComponent );
            info._addComponent       = &GameObject::addComponentTo<EnumShiftComponent>;
            info._listProperty       = {
                {hashed_string( "_color" ), hashed_string( "sw::StateShiftColor" ), SW_OFFSET_OF( EnumShiftComponent, _color )},
                {hashed_string( "_after" ),               hashed_string( "int32" ), SW_OFFSET_OF( EnumShiftComponent, _after )}
            };
            engine::getTypeRegistry().registerClass( info );
            return engine::getTypeRegistry().findType( hashed_string( "sw::EnumShiftComponent" ) );
        }();
        return s_pType;
    }
} // namespace sw

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
 * @brief [ObjectStateRoundTripTest] 제자리 로드가 실패하면 오브젝트는 읽기 전 그대로다 — 빈 오브젝트를 남기지 않는다
 * @details 제자리 로드는 컴포넌트를 먼저 비우고 읽는다. 예전에는 읽기가 실패하면 **빈 오브젝트**가 남았고, 되돌리기 · 복제 · 프리팹 되돌리기가
 *          결과를 버려 그 상태로 저장되었다. 이제 읽기 전 상태를 찍어 두고 실패하면 그것으로 되돌린다(컴포넌트 id 도 그대로).
 */
SW_TEST_CASE( ObjectStateRoundTripTest, FailedInPlaceLoadLeavesTheObjectAsItWas )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj  = manager.createGameObject( sw::hashed_string( "Keeper" ) );
    sw::SceneComponent*   pRoot = pObj->addComponent<sw::SceneComponent>();
    sw::SceneComponent*   pArm  = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pArm->attachToComponent( pRoot ) );
    pRoot->setLocalPosition( sw::float3( 1.0f, 2.0f, 3.0f ) );
    const sw::ComponentHandle rootHandle = pRoot->getHandle();

    {
        test::ScopedDefensiveTestLog expected( "a malformed state is not applied" );
        SW_EXPECT_FALSE( sw::ObjectStateSerializer::loadFromXmlString( pObj, "<GameObject _name=\"Broken\"><_listComponent><SceneComponent" ) );
        SW_EXPECT_FALSE( sw::ObjectStateSerializer::loadFromJsonString( pObj, "{ \"_name\": " ) );
    }

    SW_EXPECT_TRUE( pObj->getName() == sw::hashed_string( "Keeper" ) );
    sw::SceneComponent* pRestoredRoot = pObj->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRestoredRoot );
    SW_EXPECT_TRUE( pRestoredRoot->getLocalPosition() == sw::float3( 1.0f, 2.0f, 3.0f ) );
    SW_EXPECT_EQUAL( size_t( 1 ), pRestoredRoot->getChildren().size() );
    SW_EXPECT_TRUE( manager.resolveComponent( rootHandle ) == pRestoredRoot ); // 같은 컴포넌트 id
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

/**
 * @brief [ObjectStateRoundTripTest] PROPERTY 하나가 사라져도 그 전의 바이너리 상태는 읽힌다 — 사라진 칸만 버린다(XML 과 같은 규칙)
 * @details 바이너리 읽기만 엄격했다. 컴포넌트 안의 모르는 칸 하나에 그 컴포넌트가, 그래서 **오브젝트 통째로** 읽기가 실패했고(XML · JSON 은 건너뛰었다),
 *          오브젝트 자기 칸이 사라지면 판 붙은 읽기가 이관 함수 없이 거절했다. 바이너리 상태가 스키마보다 오래 사는 자리 — 디스크의 세이브, 핫 리로드
 *          뒤 Stop 이 되살리는 플레이 스냅샷 — 에서 그 오브젝트가 모두 사라졌다. 이제 세 형식이 같다: 지금 타입에 없는 칸은 건너뛰고 나머지를 읽는다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, StateSavedBeforeAPropertyWasRemovedStillLoads )
{
    struct RestoreTypeOnExit
    {
        TypeInfo _original;
        ~RestoreTypeOnExit() { engine::getTypeRegistry().registerClass( _original ); }
    };
    const TypeInfo* pComponentType = SchemaShiftComponent::StaticType();
    const TypeInfo* pObjectType    = GameObject::StaticType();
    SW_ASSERT_TRUE( pComponentType != nullptr && pObjectType != nullptr );
    const RestoreTypeOnExit restoreComponentType{ *pComponentType };
    const RestoreTypeOnExit restoreObjectType{ *pObjectType };

    GameObjectManager     manager;
    GameObject*           pSaved = manager.createGameObject( hashed_string( "Saved" ) );
    SchemaShiftComponent* pComp  = pSaved->addComponent<SchemaShiftComponent>();
    SW_ASSERT_NOT_NULL( pComp );
    pComp->_kept    = 7;
    pComp->_dropped = 9;
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pSaved, bytes ) );
    const string xml = ObjectStateSerializer::saveToXmlString( pSaved );

    // 1) 다음 빌드(핫 리로드)에서 컴포넌트의 `_dropped` 가 사라졌다 — 같은 이름으로 다시 등록한다.
    TypeInfo componentAfter = restoreComponentType._original;
    componentAfter._listProperty.erase( componentAfter._listProperty.begin() );
    engine::getTypeRegistry().registerClass( componentAfter );

    GameObject* pFromBinary = manager.createGameObject( hashed_string( "FromBinary" ) );
    GameObject* pFromXml    = manager.createGameObject( hashed_string( "FromXml" ) );
    size_t      readBytes   = 0;
    bool        bXmlLoaded  = false;
    {
        test::ScopedDefensiveTestLog expected( "a saved component field the type no longer declares" );
        readBytes  = ObjectStateSerializer::loadFromBinaryBuffer( pFromBinary, bytes.data(), bytes.size() );
        bXmlLoaded = ObjectStateSerializer::loadFromXmlString( pFromXml, xml );
    }
    SW_EXPECT_EQUAL( bytes.size(), readBytes );
    SW_EXPECT_TRUE( bXmlLoaded );
    const SchemaShiftComponent* pFromBinaryComp = pFromBinary->getComponent<SchemaShiftComponent>();
    const SchemaShiftComponent* pFromXmlComp    = pFromXml->getComponent<SchemaShiftComponent>();
    SW_ASSERT_TRUE( pFromBinaryComp != nullptr && pFromXmlComp != nullptr );
    SW_EXPECT_EQUAL( 7, pFromBinaryComp->_kept );
    SW_EXPECT_EQUAL( 7, pFromXmlComp->_kept );

    // 2) 오브젝트 자기 칸(`_bActive`)이 사라져도 같다.
    TypeInfo objectAfter = restoreObjectType._original;
    for ( auto iter = objectAfter._listProperty.begin(); iter != objectAfter._listProperty.end(); ++iter )
    {
        if ( iter->_name == hashed_string( "_bActive" ) )
        {
            objectAfter._listProperty.erase( iter );
            break;
        }
    }
    SW_ASSERT_EQUAL( restoreObjectType._original._listProperty.size() - 1, objectAfter._listProperty.size() );
    engine::getTypeRegistry().registerClass( objectAfter );

    GameObject* pAfterObjectShift = manager.createGameObject( hashed_string( "AfterObjectShift" ) );
    {
        test::ScopedDefensiveTestLog expected( "a saved object field the type no longer declares" );
        readBytes = ObjectStateSerializer::loadFromBinaryBuffer( pAfterObjectShift, bytes.data(), bytes.size() );
    }
    SW_EXPECT_EQUAL( bytes.size(), readBytes );
    const SchemaShiftComponent* pAfterObjectShiftComp = pAfterObjectShift->getComponent<SchemaShiftComponent>();
    SW_ASSERT_NOT_NULL( pAfterObjectShiftComp );
    SW_EXPECT_EQUAL( 7, pAfterObjectShiftComp->_kept );

    // 3) 판(스키마 버전)이 다른 상태는 여전히 받지 않는다 — 건너뛰기는 같은 판의 지운 칸만이다(판이 다르면 진짜 이관이 필요하다).
    vector<uint8> otherVersion  = bytes;
    const size_t  versionOffset = sizeof( uint32 ) * 2; // 옛 부모 이름(루트라 길이 0) · 본문 크기 다음
    uint32        savedVersion{ 0 };
    SW_ASSERT_TRUE( otherVersion.size() > versionOffset + sizeof( uint32 ) );
    Memory::copy( &savedVersion, otherVersion.data() + versionOffset, sizeof( uint32 ) );
    const uint32 newerVersion = savedVersion + 1;
    Memory::copy( otherVersion.data() + versionOffset, &newerVersion, sizeof( uint32 ) );
    GameObject* pOtherVersion = manager.createGameObject( hashed_string( "OtherVersion" ) );
    {
        test::ScopedDefensiveTestLog expected( "a state from another schema version" );
        SW_EXPECT_EQUAL( size_t( 0 ), ObjectStateSerializer::loadFromBinaryBuffer( pOtherVersion, otherVersion.data(), otherVersion.size() ) );
    }
}

/**
 * @brief [ObjectStateRoundTripTest] 끈 컴포넌트와 숨긴 메시는 상태를 건너도 그대로다 — 저장 · 되돌리기 · Stop · 씬 다시 열기
 * @details `Component::_bActive` 와 `MeshComponent::_bVisible` 은 PROPERTY 가 아니었다. 상태(씬 파일 · 되돌리기 스냅샷 · 플레이 스냅샷)가 이 값을 싣지
 *          않아, 끈 컴포넌트 · 숨긴 메시가 Stop · 되돌리기 · 씬 다시 열기 뒤 다시 켜졌고 토글은 되돌리기에 남지 않았다(앞뒤 스냅샷이 같았다).
 */
SW_TEST_CASE( ObjectStateRoundTripTest, TurnedOffComponentsAndHiddenMeshesStayThatWay )
{
    GameObjectManager manager;
    GameObject*       pSource = manager.createGameObject( hashed_string( "Lamp" ) );
    MeshComponent*    pMesh   = pSource->addComponent<MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setActive( false );
    pMesh->setVisible( false );

    const string  xml = ObjectStateSerializer::saveToXmlString( pSource );
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pSource, bytes ) );

    GameObject* pFromXml    = manager.createGameObject( hashed_string( "FromXml" ) );
    GameObject* pFromBinary = manager.createGameObject( hashed_string( "FromBinary" ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pFromXml, xml ) );
    SW_ASSERT_EQUAL( bytes.size(), ObjectStateSerializer::loadFromBinaryBuffer( pFromBinary, bytes.data(), bytes.size() ) );
    for ( GameObject* pLoaded : { pFromXml, pFromBinary } )
    {
        const MeshComponent* pLoadedMesh = pLoaded->getComponent<MeshComponent>();
        SW_ASSERT_NOT_NULL( pLoadedMesh );
        SW_EXPECT_FALSE( pLoadedMesh->isSelfActive() );
        SW_EXPECT_FALSE( pLoadedMesh->isVisible() );
    }
}

/**
 * @brief [ObjectStateRoundTripTest] 오브젝트의 둘째 씬 컴포넌트는 primary 아래에 붙는다 — 오브젝트를 옮기면 따라오고, 루트로 저장된 옛 데이터도 읽으면 붙는다
 * @details 둘째 씬 컴포넌트(콜라이더 · 소켓 · 메시)는 붙이지 않으면 루트로 남아 **월드 원점**에 놓였다 — 오브젝트를 옮겨도 따라오지 않았다.
 *          `editortest.scene.xml` 의 TestCollider(primary 는 x=2.5, 콜라이더는 원점) · `testprop.prefab.xml` 이 이미 그랬다. 오브젝트의 루트는 하나다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, SecondSceneComponentHangsUnderThePrimary )
{
    GameObjectManager manager;
    GameObject*       pCrate    = manager.createGameObject( hashed_string( "Crate" ) );
    SceneComponent*   pRoot     = pCrate->addComponent<SceneComponent>();
    SceneComponent*   pCollider = pCrate->addComponent<SceneComponent>();
    SW_ASSERT_TRUE( pRoot != nullptr && pCollider != nullptr );
    SW_EXPECT_TRUE( pCollider->getParent() == pRoot );
    pRoot->setLocalPosition( float3( 2.5f, 0.0f, 0.0f ) );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 2.5f, pCollider->getWorldPosition()._x, 1e-4f );

    // 옛 데이터: 둘째가 루트로 저장됐다. 읽으면 primary 아래로 간다.
    pCollider->detachFromComponent();
    const string oldXml  = ObjectStateSerializer::saveToXmlString( pCrate );
    GameObject*  pLoaded = manager.createGameObject( hashed_string( "LoadedCrate" ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pLoaded, oldXml ) );
    SceneComponent* pLoadedRoot = pLoaded->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pLoadedRoot );
    SceneComponent* pLoadedCollider = nullptr;
    pLoaded->forEachComponentOfType<SceneComponent>( [&]( SceneComponent* pScene )
    {
        if ( pScene != pLoadedRoot )
            pLoadedCollider = pScene;
    } );
    SW_ASSERT_NOT_NULL( pLoadedCollider );
    SW_EXPECT_TRUE( pLoadedCollider->getParent() == pLoadedRoot );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 2.5f, pLoadedCollider->getWorldPosition()._x, 1e-4f );
}

/**
 * @brief [ObjectStateRoundTripTest] 부착 규칙 — `KeepWorld` 는 돌고 커진 부모에 붙이고 떼어도 월드 자리를 지키고, 기본(`KeepRelative`)은 로컬을 지킨다
 * @details 규칙이 하나(로컬 지킴)뿐이라 에디터의 재부모 · 부모 떼기가 오브젝트를 새 부모만큼 튀게 했다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, AttachRuleKeepsWorldOrRelativeAsAsked )
{
    GameObjectManager manager;
    GameObject*       pParentObj = manager.createGameObject( hashed_string( "Turntable" ) );
    SceneComponent*   pParent    = pParentObj->addComponent<SceneComponent>();
    pParent->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    pParent->setLocalRotation( float3( 0.0f, MathUtil::HalfPi, 0.0f ) );
    pParent->setLocalScale( float3( 2.0f, 2.0f, 2.0f ) );
    GameObject*     pChildObj = manager.createGameObject( hashed_string( "Vase" ) );
    SceneComponent* pChild    = pChildObj->addComponent<SceneComponent>();
    pChild->setLocalPosition( float3( 3.0f, 1.0f, -2.0f ) );
    manager.flushSceneTransforms();

    SW_ASSERT_TRUE( pChildObj->attachToParent( pParentObj, AttachRule::KeepWorld ) );
    manager.flushSceneTransforms();
    const float3 worldAfterAttach = pChild->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 3.0f, worldAfterAttach._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, worldAfterAttach._y, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, worldAfterAttach._z, 1e-3f );

    pChildObj->detachFromParent( AttachRule::KeepWorld );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 3.0f, pChild->getWorldPosition()._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, pChild->getWorldPosition()._z, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pChild->getLocalPosition()._x, 1e-3f );

    // 기본은 로컬을 지킨다 — 상태 읽기가 기대하는 규칙이다.
    SW_ASSERT_TRUE( pChildObj->attachToParent( pParentObj ) );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 3.0f, pChild->getLocalPosition()._x, 1e-4f );
    SW_EXPECT_TRUE( MathUtil::abs( pChild->getWorldPosition()._x - 3.0f ) > 1.0f );
}

/**
 * @brief [ObjectStateRoundTripTest] 모르는 타입의 컴포넌트는 버려지지 않는다 — 다시 저장하면 원문 그대로 남는다(XML · JSON · 바이너리)
 * @details 세 직렬화기가 모르는 타입의 원소를 건너뛰었다. 게임 모듈이 안 뜬 채 에디터가 씬을 저장하면 그 컴포넌트의 값이 **영영** 사라졌다.
 *          이제 원문을 `MissingComponent` 가 맡고, 같은 형식으로 저장할 때 그대로 다시 쓴다. 다른 형식(플레이 스냅샷 · 되돌리기 = 바이너리)을
 *          거쳐도 원래 형식으로 돌아오면 원문이 돌아간다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, UnknownComponentIsKeptAndWrittenBack )
{
    GameObjectManager manager;
    GameObject*       pSource = manager.createGameObject( hashed_string( "Lamp" ) );
    SW_ASSERT_NOT_NULL( pSource->addComponent<SceneComponent>() );

    // XML: 모르는 원소를 하나 끼운다(모듈이 안 뜬 컴포넌트).
    string       xml      = ObjectStateSerializer::saveToXmlString( pSource );
    const size_t listOpen = xml.find( "<_listComponent>" );
    SW_ASSERT_TRUE( listOpen != string::npos );
    const string kUnknownXml = "<NotLoadedLampDriver _flicker=\"0.25\" _mode=\"Candle\" />";
    xml.insert( listOpen + string_view( "<_listComponent>" ).size(), kUnknownXml );

    GameObject* pFromXml = manager.createGameObject( hashed_string( "FromXml" ) );
    {
        test::ScopedDefensiveTestLog expected( "a component type that is not loaded" );
        SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pFromXml, xml ) );
    }
    const MissingComponent* pMissing = pFromXml->getComponent<MissingComponent>();
    SW_ASSERT_NOT_NULL( pMissing );
    SW_EXPECT_STREQ( "NotLoadedLampDriver", pMissing->getOriginalTypeName().c_str() );
    const string savedAgain = ObjectStateSerializer::saveToXmlString( pFromXml );
    SW_EXPECT_TRUE( savedAgain.find( "<NotLoadedLampDriver" ) != string::npos );
    SW_EXPECT_TRUE( savedAgain.find( "_flicker=\"0.25\"" ) != string::npos );
    SW_EXPECT_TRUE( savedAgain.find( "MissingComponent" ) == string::npos ); // 자리 표시가 아니라 원문이 나간다

    // 바이너리를 거쳐(플레이 스냅샷) 다시 XML 로 — 원문이 돌아온다.
    vector<uint8> snapshot;
    SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pFromXml, snapshot ) );
    GameObject* pRestored = manager.createGameObject( hashed_string( "Restored" ) );
    SW_ASSERT_EQUAL( snapshot.size(), ObjectStateSerializer::loadFromBinaryBuffer( pRestored, snapshot.data(), snapshot.size() ) );
    SW_EXPECT_TRUE( ObjectStateSerializer::saveToXmlString( pRestored ).find( "_flicker=\"0.25\"" ) != string::npos );

    // JSON
    string       json     = ObjectStateSerializer::saveToJsonString( pSource );
    const size_t jsonList = json.find( "\"_listComponent\"" );
    SW_ASSERT_TRUE( jsonList != string::npos );
    const size_t arrayOpen = json.find( '[', jsonList );
    SW_ASSERT_TRUE( arrayOpen != string::npos );
    json.insert( arrayOpen + 1, "{\"NotLoadedLampDriver\":{\"_flicker\":0.25}}," );
    GameObject* pFromJson = manager.createGameObject( hashed_string( "FromJson" ) );
    {
        test::ScopedDefensiveTestLog expected( "a component type that is not loaded" );
        SW_ASSERT_TRUE( ObjectStateSerializer::loadFromJsonString( pFromJson, json ) );
    }
    const string jsonAgain = ObjectStateSerializer::saveToJsonString( pFromJson );
    SW_EXPECT_TRUE( jsonAgain.find( "NotLoadedLampDriver" ) != string::npos );
    SW_EXPECT_TRUE( jsonAgain.find( "_flicker" ) != string::npos );
    SW_EXPECT_TRUE( jsonAgain.find( "MissingComponent" ) == string::npos ); // 자리 표시가 아니라 원문이 나간다

    // 바이너리: 원소 이름을 같은 길이의 모르는 이름으로 바꾼다(지운 타입의 세이브).
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pSource, bytes ) );
    const string_view kKnown   = "SceneComponent";
    const string_view kUnknown = "SceneComponenX";
    auto              nameAt   = std::search( bytes.begin(), bytes.end(), kKnown.begin(), kKnown.end() );
    SW_ASSERT_TRUE( nameAt != bytes.end() );
    std::copy( kUnknown.begin(), kUnknown.end(), nameAt );
    GameObject* pFromBinary = manager.createGameObject( hashed_string( "FromBinary" ) );
    {
        test::ScopedDefensiveTestLog expected( "a component type that is not loaded" );
        SW_ASSERT_EQUAL( bytes.size(), ObjectStateSerializer::loadFromBinaryBuffer( pFromBinary, bytes.data(), bytes.size() ) );
    }
    SW_ASSERT_NOT_NULL( pFromBinary->getComponent<MissingComponent>() );
    vector<uint8> bytesAgain;
    SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pFromBinary, bytesAgain ) );
    auto unknownAt = std::search( bytesAgain.begin(), bytesAgain.end(), kUnknown.begin(), kUnknown.end() );
    SW_ASSERT_TRUE( unknownAt != bytesAgain.end() );

    // 모듈이 돌아왔다(이름을 되돌린다) — 원래 컴포넌트로 읽힌다. 자리 표시가 자기 자신으로 저장됐다면 다시 MissingComponent 가 된다.
    std::copy( kKnown.begin(), kKnown.end(), unknownAt );
    GameObject* pModuleBack = manager.createGameObject( hashed_string( "ModuleBack" ) );
    SW_ASSERT_EQUAL( bytesAgain.size(), ObjectStateSerializer::loadFromBinaryBuffer( pModuleBack, bytesAgain.data(), bytesAgain.size() ) );
    SW_EXPECT_NULL( pModuleBack->getComponent<MissingComponent>() );
    SW_EXPECT_NOT_NULL( pModuleBack->getComponent<SceneComponent>() );
}

/**
 * @brief [ObjectStateRoundTripTest] 저장된 상태의 enum 은 열거자로 돌아온다 — 다음 빌드가 열거자 순서를 바꿔도, 열거자를 지워도(그 칸만 기본값, 나머지는 그대로)
 * @details 바이너리 상태(세이브 · 핫 리로드 뒤 Stop 이 되살리는 플레이 스냅샷 · 되돌리기)는 enum 을 int64 **값**으로 실었다. 열거자를 사이에 넣으면 저장된
 *          Green 이 다른 열거자로 읽혔다. 지운 열거자는 그 값의 다른 열거자가 됐다 — XML 은 그 칸만 실패로 남긴다. 그리고 바이너리는 컴포넌트 안의 읽지 못한 칸 하나에
 *          그 컴포넌트를, 그래서 뒤의 컴포넌트까지 버렸다. 시험은 손으로 올린 enum 을 저장 뒤에 같은 이름으로 다시 올린다(핫 리로드 · 다음 빌드의 자리).
 */
SW_TEST_CASE( ObjectStateRoundTripTest, SavedEnumsKeepTheirEnumeratorAcrossEnumChanges )
{
    struct RestoreEnumOnExit
    {
        ~RestoreEnumOnExit() { registerStateShiftColorAsDeclared(); }
    };
    registerStateShiftColorAsDeclared();
    const RestoreEnumOnExit restoreEnum{};

    GameObjectManager     manager;
    GameObject*           pSaved = manager.createGameObject( hashed_string( "Saved" ) );
    EnumShiftComponent*   pEnum  = pSaved->addComponent<EnumShiftComponent>();
    SchemaShiftComponent* pNext  = pSaved->addComponent<SchemaShiftComponent>();
    SW_ASSERT_TRUE( pEnum != nullptr && pNext != nullptr );
    pEnum->_color = StateShiftColor::Green;
    pEnum->_after = 5;
    pNext->_kept  = 7;
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pSaved, bytes ) );
    const string xml = ObjectStateSerializer::saveToXmlString( pSaved );

    // 1) 다음 빌드에서 열거자 순서가 바뀌었다 — Green 은 이제 2 다.
    registerStateShiftColor( {
        { "Blue", 0},
        {  "Red", 1},
        {"Green", 2}
    } );
    GameObject* pReordered = manager.createGameObject( hashed_string( "Reordered" ) );
    SW_EXPECT_EQUAL( bytes.size(), ObjectStateSerializer::loadFromBinaryBuffer( pReordered, bytes.data(), bytes.size() ) );
    const EnumShiftComponent* pReorderedEnum = pReordered->getComponent<EnumShiftComponent>();
    SW_ASSERT_NOT_NULL( pReorderedEnum );
    SW_EXPECT_TRUE_MSG( static_cast<int32>( pReorderedEnum->_color ) == 2, "저장된 Green 이 값으로 읽혀 다른 열거자가 됐습니다" );
    SW_EXPECT_EQUAL( 5, pReorderedEnum->_after );
    GameObject* pReorderedXml = manager.createGameObject( hashed_string( "ReorderedXml" ) );
    SW_EXPECT_TRUE( ObjectStateSerializer::loadFromXmlString( pReorderedXml, xml ) );
    const EnumShiftComponent* pReorderedXmlEnum = pReorderedXml->getComponent<EnumShiftComponent>();
    SW_ASSERT_NOT_NULL( pReorderedXmlEnum );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( pReorderedXmlEnum->_color ) ); // XML 은 원래 이름으로 실었다 — 두 형식이 같은 답이다

    // 2) 다음 빌드에서 Green 이 Lime 이 됐다(ValueAlias 없음). 그 칸만 기본값(Red)으로 남고, 뒤의 칸 · 뒤의 컴포넌트는 읽힌다.
    registerStateShiftColor( {
        { "Red", 0},
        {"Lime", 1},
        {"Blue", 2}
    } );
    GameObject* pRenamed    = manager.createGameObject( hashed_string( "Renamed" ) );
    GameObject* pRenamedXml = manager.createGameObject( hashed_string( "RenamedXml" ) );
    size_t      readBytes   = 0;
    bool        bXmlLoaded  = false;
    {
        test::ScopedDefensiveTestLog expected( "a saved enumerator the enum no longer has" );
        readBytes  = ObjectStateSerializer::loadFromBinaryBuffer( pRenamed, bytes.data(), bytes.size() );
        bXmlLoaded = ObjectStateSerializer::loadFromXmlString( pRenamedXml, xml );
    }
    SW_EXPECT_EQUAL( bytes.size(), readBytes );
    SW_EXPECT_TRUE( bXmlLoaded );
    for ( GameObject* pRenamedObject : { pRenamed, pRenamedXml } )
    {
        const EnumShiftComponent*   pRenamedEnum = pRenamedObject->getComponent<EnumShiftComponent>();
        const SchemaShiftComponent* pRenamedNext = pRenamedObject->getComponent<SchemaShiftComponent>();
        SW_ASSERT_TRUE_MSG( pRenamedEnum != nullptr && pRenamedNext != nullptr, "읽지 못한 enum 칸 하나에 컴포넌트가 사라졌습니다" );
        SW_EXPECT_TRUE_MSG( pRenamedEnum->_color == StateShiftColor::Red, "지운 열거자가 그 값의 다른 열거자(Lime)로 읽혔습니다" );
        SW_EXPECT_EQUAL( 5, pRenamedEnum->_after );
        SW_EXPECT_EQUAL( 7, pRenamedNext->_kept );
    }
}

/**
 * @brief [ObjectStateRoundTripTest] JSON 컴포넌트 안의 못 읽은 칸은 그 칸만 버려지고 이름으로 알린다 — 컴포넌트의 나머지는 읽힌다
 * @details JSON 은 컴포넌트 원소를 orphan 목록 없이 엄격하게 읽었다. 칸 하나가 `_listComponent` 칸 **전체**를 실패로 만들어 로드는
 *          "_listComponent 를 버렸다" 고 알렸다 — 컴포넌트는 읽혔는데 어느 칸이 문제인지는 말하지 않았다. XML 은 처음부터 바깥 목록을 내려 줬다.
 */
SW_TEST_CASE( ObjectStateRoundTripTest, JsonComponentFieldThatDoesNotReadIsNamed )
{
    GameObjectManager manager;
    GameObject*       pSource = manager.createGameObject( hashed_string( "Mover" ) );
    SceneComponent*   pRoot   = pSource->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalScale( float3( 2.0f, 2.0f, 2.0f ) );

    string       json    = ObjectStateSerializer::saveToJsonString( pSource );
    const size_t typeKey = json.find( "\"SceneComponent\"" );
    SW_ASSERT_TRUE( typeKey != string::npos );
    const size_t bodyOpen = json.find( '{', typeKey );
    SW_ASSERT_TRUE( bodyOpen != string::npos );
    json.insert( bodyOpen + 1, "\"_noSuchField\":1," );

    GameObject*              pLoaded = manager.createGameObject( hashed_string( "Loaded" ) );
    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "a component field the type does not have" );
        SW_ASSERT_TRUE( ObjectStateSerializer::loadFromJsonString( pLoaded, json ) );
    }
    const SceneComponent* pLoadedRoot = pLoaded->getComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pLoadedRoot );
    SW_EXPECT_TRUE( pLoadedRoot->getLocalScale() == float3( 2.0f, 2.0f, 2.0f ) );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "_noSuchField" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "_listComponent" ) == 0, logs.joined().c_str() );
}
