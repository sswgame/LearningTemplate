#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

// 메시 컴포넌트 — 머티리얼 참조를 어떻게 푸는가.

/**
 * @brief [MeshComponentTest] 못 읽은 머티리얼은 누락 머티리얼(마젠타 체커)로 그린다 — 씬 기본(흰색)이면 화면에서 빠진 것을 알 수 없다
 * @details 같은 요청을 다시 풀어도 누락 머티리얼 그대로이고 다시 시도하지 않는다(요청 경로를 기억한다). 경로를 고치면 그 머티리얼로 옮겨 간다.
 *          디바이스가 없어 머티리얼 파일을 읽지 않으므로(이름이 비어 있다) 어느 머티리얼을 잡았는지는 캐시 항목으로 본다.
 */
SW_TEST_CASE( MeshComponentTest, MissingMaterialUsesTheChecker )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string& missingMaterial = sw::engine::getEngineDefaultAssets()._missingMaterial;
    SW_ASSERT_FALSE( missingMaterial.empty() );

    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Crate" ) );
    SW_ASSERT_NOT_NULL( pObject );
    sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );

    {
        SW_TEST_DEFENSIVE_SCOPE( "the mesh names a material that does not exist" );
        pMesh->setMaterialPath( "engine/materials/doesnotexist.material" );
    }
    SW_ASSERT_NOT_NULL( pMesh->getMaterial() );
    SW_EXPECT_FALSE( sw::engine::getAssetManager().getMaterialManager().isCached( "engine/materials/doesnotexist.material" ) );
    SW_EXPECT_TRUE( sw::engine::getAssetManager().getMaterialManager().isCached( missingMaterial ) );

    // 같은 요청을 다시 풀면 그대로다(누락 머티리얼을 다시 빌리지 않는다).
    sw::Material* pFirst = pMesh->getMaterial();
    pMesh->resolveRenderAssets();
    SW_EXPECT_TRUE( pMesh->getMaterial() == pFirst );

    // 경로를 고치면 그 머티리얼로 옮겨 간다.
    pMesh->setMaterialPath( "engine/materials/defaultmaterial.material" );
    SW_ASSERT_NOT_NULL( pMesh->getMaterial() );
    SW_EXPECT_TRUE( pMesh->getMaterial() != pFirst );
    SW_EXPECT_TRUE( sw::engine::getAssetManager().getMaterialManager().isCached( "engine/materials/defaultmaterial.material" ) );
}
