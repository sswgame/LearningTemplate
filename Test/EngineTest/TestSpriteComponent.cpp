#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

// 스프라이트 컴포넌트 — 무엇으로 그려지는가(메시 · 머티리얼 · 텍스처).

namespace
{
    constexpr const utf8* kSpriteMaterialPath = "engine/materials/sprite2d.material";
    constexpr const utf8* kTextureA           = "engine/textures/flashlight.png";
    constexpr const utf8* kTextureB           = "engine/textures/light_bulb.png";

    /** @brief 텍스처를 건 스프라이트 하나를 만들고 렌더 에셋을 풉니다(시작 · 씬 초기화가 부르는 자리). */
    sw::SpriteComponent* spawnSprite( sw::GameObjectManager& manager, const utf8* pName, const utf8* pTexture )
    {
        sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        sw::SpriteComponent* pSprite = pObject->addComponent<sw::SpriteComponent>();
        if ( pSprite == nullptr )
            return nullptr;
        pSprite->setTextureName( pTexture );
        pSprite->resolveRenderAssets();
        return pSprite;
    }
} // namespace

/**
 * @brief [SpriteComponentTest] 스프라이트는 스프라이트 머티리얼을 입은 사각형으로 그려지고, 텍스처는 그 머티리얼의 인스턴스가 덮어쓴다
 * @details 예전에는 스프라이트가 메시 컴포넌트의 기본을 그대로 따라 **씬 기본 머티리얼의 단위 큐브**로 그려졌다. 텍스처 · 메시 · 머티리얼 이름 칸은
 *          저장만 되고 읽는 곳이 없었고, `sprite2d.material` 과 그 셰이더는 구워지기만 했다. 유니티 `SpriteRenderer`(스프라이트 기본 머티리얼 ·
 *          텍스처는 머티리얼 프로퍼티 블록) · 언리얼 Paper2D(`UPaperSpriteComponent` — 사각형 + 스프라이트 머티리얼 + 텍스처)와 같은 모양으로 바꿨다.
 */
SW_TEST_CASE( SpriteComponentTest, SpriteDrawsATexturedQuadWithTheSpriteMaterial )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::SpriteComponent*  pSprite = spawnSprite( manager, "Torch", kTextureA );
    SW_ASSERT_NOT_NULL( pSprite );

    // 사각형(정점 여섯 — `sprite2d.hlsl` 이 위치 [-0.5, 0.5] 로 UV 를 짓는다). 예전에는 빈 메시 id 가 큐브로 풀렸다.
    SW_ASSERT_NOT_NULL( pSprite->getRawMesh() );
    SW_EXPECT_EQUAL( 6u, pSprite->getRawMesh()->getVertexCount() );

    // 스프라이트 머티리얼. 저장된 머티리얼 참조가 비어 있으면 이것이 기본이다.
    SW_ASSERT_NOT_NULL( pSprite->getMaterial() );
    SW_EXPECT_TRUE( sw::engine::getResourceManager().getMaterialManager().isCached( kSpriteMaterialPath ) );

    // 텍스처는 그 머티리얼의 인스턴스로 덮어쓴다.
    sw::MaterialInstance* pInstance = pSprite->getRawMaterialInstance();
    SW_ASSERT_NOT_NULL( pInstance );
    SW_EXPECT_TRUE( pInstance->getParent() == pSprite->getMaterial() );
    SW_EXPECT_STREQ( kTextureA, pInstance->getTextureParameter( sw::hashed_string( "albedoMap" ) ).c_str() );
}

/**
 * @brief [SpriteComponentTest] 같은 텍스처의 스프라이트는 인스턴스 하나를 나눠 쓴다 — 배치 키가 인스턴스라 그래야 한 드로우로 묶인다
 * @details 텍스처를 바꾸면 그 텍스처의 인스턴스로 옮겨 가고, 텍스처를 비우면 인스턴스 없이 스프라이트 머티리얼(흰 사각형)로 그린다.
 */
SW_TEST_CASE( SpriteComponentTest, SpritesWithTheSameTextureShareOneInstance )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::SpriteComponent*  pFirst  = spawnSprite( manager, "TorchA", kTextureA );
    sw::SpriteComponent*  pSecond = spawnSprite( manager, "TorchB", kTextureA );
    sw::SpriteComponent*  pBulb   = spawnSprite( manager, "Bulb", kTextureB );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_ASSERT_NOT_NULL( pSecond );
    SW_ASSERT_NOT_NULL( pBulb );
    SW_ASSERT_NOT_NULL( pFirst->getRawMaterialInstance() );
    SW_EXPECT_TRUE( pFirst->getRawMaterialInstance() == pSecond->getRawMaterialInstance() );
    SW_EXPECT_TRUE( pFirst->getRawMaterialInstance() != pBulb->getRawMaterialInstance() );

    // 텍스처를 바꾸면 그 텍스처의 인스턴스로 간다.
    pSecond->setTextureName( kTextureB );
    SW_EXPECT_TRUE( pSecond->getRawMaterialInstance() == pBulb->getRawMaterialInstance() );

    // 텍스처를 비우면 인스턴스 없이 그린다.
    pBulb->setTextureName( "" );
    SW_EXPECT_TRUE( pBulb->getRawMaterialInstance() == nullptr );
    SW_EXPECT_TRUE( pBulb->getMaterial() == pFirst->getMaterial() );
}

/**
 * @brief [SpriteComponentTest] 스프라이트가 따로 들던 메시 이름 칸(`_meshName`)은 메시 컴포넌트의 메시 id 로 읽힌다
 * @details 두 칸이 같은 것을 들었고 스프라이트 칸은 읽는 곳이 없었다. 하나로 합쳤고, 옛 파일은 옛 이름으로 읽힌다.
 */
SW_TEST_CASE( SpriteComponentTest, OldMeshNameLoadsIntoTheMeshId )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "OldSprite" ) );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString(
        pObject, "<GameObject _name=\"OldSprite\"><_listComponent><SpriteComponent _meshName=\"Plane\" /></_listComponent></GameObject>" ) );
    const sw::SpriteComponent* pSprite = pObject->getComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pSprite );
    SW_EXPECT_STREQ( "Plane", pSprite->getMeshId().c_str() );
}
