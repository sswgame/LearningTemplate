#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/AssetHotReload.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/IAssetCache.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

// AssetHotReloadTest — 에디터 핫 리로드의 씬 알림(누가 그 에셋을 쓰는지 리플렉션으로 찾는다).
/**
 * @brief [AssetHotReloadTest] 다시 읽은 에셋은 그 경로를 에셋 경로 프로퍼티로 든 컴포넌트에만 `onPropertyChanged` 로 알려진다
 * @details 컴포넌트에는 리로드 전용 코드가 없다 — 에디터가 `PROPERTY( AssetPath )` 값을 보고 인스펙터 편집과 같은 알림을 보낸다.
 *          경로 구분자가 달라도 같은 에셋이다.
 */

SW_TEST_CASE( AssetHotReloadTest, ReloadedAssetReachesComponentsThroughTheirAssetPathProperty )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string    clipPath = test::makeTempPath( "hot_reload_users.sprite.json" );
    sw::SpriteClipAsset clip;
    clip._listFrame.resize( 2 );
    clip._listFrame[1]._uvRect = sw::float4{ 0.0f, 0.0f, 0.5f, 0.5f };
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );

    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Sprite" ) );
    SW_ASSERT_NOT_NULL( pObject );
    sw::SpriteComponent* pSprite = pObject->addComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pSprite );
    pSprite->setClipPath( clipPath );
    pSprite->setClipFrame( 1 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );

    clip._listFrame[1]._uvRect = sw::float4{ 0.5f, 0.5f, 0.5f, 0.5f };
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );
    sw::ResourceManager resources;
    sw::IAssetCache*    pCache = resources.findAssetCache( "SpriteClip" );
    SW_ASSERT_NOT_NULL( pCache );
    pCache->reload( clipPath, nullptr );

    // 다른 경로는 아무에게도 알리지 않는다.
    SW_EXPECT_EQUAL( 0u, sw::editor::AssetHotReload::notifyAssetUsers( manager, "other/unrelated.sprite.json" ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );

    // 구분자가 달라도 같은 경로다 — 클립 프로퍼티 하나에 알린다.
    sw::string spelledDifferently = clipPath;
    for ( utf8& ch : spelledDifferently )
    {
        if ( ch == '/' )
            ch = '\\';
    }
    SW_EXPECT_EQUAL( 1u, sw::editor::AssetHotReload::notifyAssetUsers( manager, spelledDifferently ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );
}
