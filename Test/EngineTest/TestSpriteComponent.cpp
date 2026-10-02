#include "pch.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
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

    // 양면 스프라이트 사각형(면마다 삼각형 둘 — 정점 열둘, `MeshUtil::createSpriteQuad`). 예전에는 빈 메시 id 가 큐브로 풀렸고, 그다음에는
    // 한 면짜리 3D 쿼드라 보이는 쪽에서 텍스처가 좌우로 뒤집혔다.
    SW_ASSERT_NOT_NULL( pSprite->getRawMesh() );
    SW_EXPECT_EQUAL( 12u, pSprite->getRawMesh()->getVertexCount() );
    SW_EXPECT_TRUE( pSprite->getMesh() == sw::MeshUtil::acquirePrimitive( "Sprite" ) );

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

/**
 * @brief [SpriteComponentTest] 프레임(UV 사각형) · 색은 머티리얼 인스턴스가 아니라 GPU 인스턴스 칸에 실린다 — 같은 텍스처의 스프라이트는 값이 달라도 인스턴스 하나다
 * @details 배치 키가 머티리얼 인스턴스라 프레임 · 색을 인스턴스 파라미터로 바꾸면 스프라이트마다 배치가 갈린다. 그래서 둘은 `GpuSpriteInstanceData`
 *          (unorm16 꼭짓점 둘 + RGBA8)로 묶여 메시 컴포넌트의 인스턴스 칸에 간다. 꼭짓점으로 담으므로 음수 폭(좌우 반전)이 그대로 들어가고, 텍스처
 *          밖으로 나간 꼭짓점은 [0, 1] 로 묶인다.
 */
SW_TEST_CASE( SpriteComponentTest, FrameAndTintTravelInTheInstanceNotTheMaterial )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::SpriteComponent*  pRed   = spawnSprite( manager, "Red", kTextureA );
    sw::SpriteComponent*  pGhost = spawnSprite( manager, "Ghost", kTextureA );
    SW_ASSERT_NOT_NULL( pRed );
    SW_ASSERT_NOT_NULL( pGhost );

    // 기본은 텍스처 전체 · 흰색 불투명이다(셰이더의 빈 인스턴스와 같은 값).
    const sw::GpuSpriteInstanceData identity{};
    SW_EXPECT_TRUE( pGhost->getSpriteInstanceData() == identity );

    pRed->setUvRect( sw::float4{ 0.25f, 0.0f, 0.25f, 1.0f } );
    pRed->setTint( sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    pGhost->setTint( sw::float4{ 0.0f, 1.0f, 0.0f, 0.5f } );

    const sw::float4 redRect = pRed->getSpriteInstanceData().getUvRect();
    SW_EXPECT_NEAR_EQUAL( 0.25f, redRect._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, redRect._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, redRect._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, redRect._w, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pRed->getSpriteInstanceData().getTint()._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pGhost->getSpriteInstanceData().getTint()._w, 1.0f / 255.0f );

    // 값이 달라도 머티리얼 인스턴스는 하나다(배치 하나).
    SW_ASSERT_NOT_NULL( pRed->getRawMaterialInstance() );
    SW_EXPECT_TRUE( pRed->getRawMaterialInstance() == pGhost->getRawMaterialInstance() );

    // 좌우 반전 — 폭이 음수인 사각형이 그대로 들어간다.
    pRed->setUvRect( sw::float4{ 0.5f, 0.0f, -0.5f, 1.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pRed->getSpriteInstanceData().getUvRect()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -0.5f, pRed->getSpriteInstanceData().getUvRect()._z, 1e-4f );
    // 텍스처 밖으로 나간 꼭짓점은 묶인다(타일링은 머티리얼 uvRect 의 일이다).
    pRed->setUvRect( sw::float4{ 0.9f, 0.0f, 0.5f, 1.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.1f, pRed->getSpriteInstanceData().getUvRect()._z, 1e-4f );
}

/**
 * @brief [SpriteComponentTest] 클립이 있으면 그 클립의 프레임을 보이고, 텍스처 칸이 비어 있으면 클립의 아틀라스를 입는다
 * @details 프레임 번호가 클립보다 크면 마지막 프레임을 보인다(애니메이터가 끝에서 멈춘 자리). 텍스처 칸을 채우면 그것이 아틀라스를 이긴다.
 *          읽을 수 없는 클립은 알리고 `_uvRect` 로 그린다.
 */
SW_TEST_CASE( SpriteComponentTest, ClipSelectsTheFrameAndLendsItsAtlas )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string    clipPath = test::makeTempPath( "atlas.sprite.json" );
    sw::SpriteClipAsset clip;
    clip._atlasPath = kTextureB;
    clip._listFrame.resize( 3 );
    clip._listFrame[1]._uvRect = sw::float4{ 0.5f, 0.0f, 0.5f, 0.5f };
    clip._listFrame[2]._uvRect = sw::float4{ 0.0f, 0.5f, 0.5f, 0.5f };
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );

    sw::GameObjectManager manager;
    sw::SpriteComponent*  pSprite = spawnSprite( manager, "Hero", "" );
    SW_ASSERT_NOT_NULL( pSprite );
    SW_EXPECT_TRUE( pSprite->getRawMaterialInstance() == nullptr );
    pSprite->setUvRect( sw::float4{ 0.0f, 0.0f, 0.25f, 0.25f } ); // 클립이 생기면 가려진다

    pSprite->setClipPath( clipPath );
    SW_ASSERT_NOT_NULL( pSprite->getClip() );
    SW_ASSERT_NOT_NULL( pSprite->getRawMaterialInstance() );
    SW_EXPECT_STREQ( kTextureB, pSprite->getRawMaterialInstance()->getTextureParameter( sw::hashed_string( "albedoMap" ) ).c_str() );

    pSprite->setClipFrame( 1 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSprite->getSpriteInstanceData().getUvRect()._w, 1e-4f );
    pSprite->setClipFrame( 10 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSprite->getSpriteInstanceData().getUvRect()._y, 1e-4f ); // 마지막 프레임

    // 텍스처 칸이 아틀라스를 이긴다.
    pSprite->setTextureName( kTextureA );
    SW_EXPECT_STREQ( kTextureA, pSprite->getRawMaterialInstance()->getTextureParameter( sw::hashed_string( "albedoMap" ) ).c_str() );

    // 읽을 수 없는 클립: 알리고 UV 사각형으로 그린다.
    {
        test::ScopedDefensiveTestLog expected( "a sprite clip that cannot be read" );
        pSprite->setClipPath( test::makeTempPath( "missing.sprite.json" ) );
    }
    SW_EXPECT_TRUE( pSprite->getClip() == nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pSprite->getSpriteInstanceData().getUvRect()._z, 1e-4f );
}

/**
 * @brief [SpriteComponentTest] 옛 칸 `_spriteName` 은 클립 경로로 읽힌다 — 표시 이름("Sprite Clip")이 말하던 것이 실제 조회 키가 됐다
 * @details 예전 칸은 저장만 되고 읽는 곳이 없었다(애니메이터가 "<애니>-<프레임>" 을 적었다). 저장된 옛 파일이 그 이름으로 읽혀 클립을 잡아야 한다.
 */
SW_TEST_CASE( SpriteComponentTest, OldSpriteNameLoadsIntoTheClipPath )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "OldClipSprite" ) );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString(
        pObject, "<GameObject _name=\"OldClipSprite\"><_listComponent><SpriteComponent _spriteName=\"engine/textures/test/quadrants.sprite.json\" "
                 "/></_listComponent></GameObject>" ) );
    const sw::SpriteComponent* pSprite = pObject->getComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pSprite );
    SW_EXPECT_STREQ( "engine/textures/test/quadrants.sprite.json", pSprite->getClipPath().c_str() );
    SW_ASSERT_NOT_NULL( pSprite->getClip() );
    SW_EXPECT_EQUAL( 4, pSprite->getClip()->getFrameCount() );
}

/**
 * @brief [SpriteComponentTest] 쥔 동안 고친 클립도 살아 있는 스프라이트에 닿는다 — 제자리 다시 읽기 뒤 프레임을 다시 맞춘다
 * @details 공유 표는 약한 참조라, 쥔 스프라이트가 하나라도 있으면 파일을 고쳐도 옛 내용을 줬다 — 플레이 중 에디터에서 클립을 저장해도 살아 있는
 *          스프라이트는 옛 프레임을 그렸다. `SpriteClipAsset::reloadShared` 가 쥔 쪽 모두의 클립을 제자리로 다시 읽고(언리얼 재임포트),
 *          `refreshFromClip` 이 아틀라스와 지금 프레임의 UV 를 다시 맞춘다(에디터 핫 리로드가 둘을 부른다).
 */
SW_TEST_CASE( SpriteComponentTest, ReloadedClipReachesSpritesThatHoldIt )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string    clipPath = test::makeTempPath( "reloaded.sprite.json" );
    sw::SpriteClipAsset clip;
    clip._atlasPath = kTextureA;
    clip._listFrame.resize( 2 );
    clip._listFrame[1]._uvRect = sw::float4{ 0.0f, 0.0f, 0.5f, 0.5f };
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );

    sw::GameObjectManager manager;
    sw::SpriteComponent*  pSprite = spawnSprite( manager, "Reloaded", "" );
    SW_ASSERT_NOT_NULL( pSprite );
    pSprite->setClipPath( clipPath );
    pSprite->setClipFrame( 1 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );

    // 쥔 채로 파일을 고친다 — 프레임 1 을 옮기고 아틀라스를 바꾼다.
    clip._atlasPath            = kTextureB;
    clip._listFrame[1]._uvRect = sw::float4{ 0.5f, 0.5f, 0.5f, 0.5f };
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::SpriteClipAsset::acquireShared( clipPath )->findFrame( 1 )->_uvRect._x, 1e-4f ); // 쥔 동안은 옛 내용

    SW_ASSERT_TRUE( sw::SpriteClipAsset::reloadShared( clipPath ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSprite->getClip()->findFrame( 1 )->_uvRect._x, 1e-4f );
    pSprite->refreshFromClip();
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );
    SW_ASSERT_NOT_NULL( pSprite->getRawMaterialInstance() );
    SW_EXPECT_STREQ( kTextureB, pSprite->getRawMaterialInstance()->getTextureParameter( sw::hashed_string( "albedoMap" ) ).c_str() );

    // 아무도 쥐지 않은 경로는 다시 읽을 것이 없다(다음에 읽는 쪽이 새 내용을 읽는다).
    SW_EXPECT_FALSE( sw::SpriteClipAsset::reloadShared( test::makeTempPath( "nobody_holds.sprite.json" ) ) );
}
