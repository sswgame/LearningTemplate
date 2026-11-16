#include "pch.h"

#include "Engine/Renderer/SceneRHISubsystem.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    SceneRHISubsystem::SceneRHISubsystem( const RHISubsystem& rhiSubsystem )
        : _rhiSubsystem{ rhiSubsystem }
        , _renderBinding{ rhiSubsystem }
    {
    }

    SubsystemInitResult SceneRHISubsystem::initializeSubsystem()
    {
        IRHIDevice* const pDevice = &_rhiSubsystem.findRHI()->getDevice();
        engine::getSubsystem<SceneManager>().setRenderBinding( &_renderBinding );
        bindAssetCaches( pDevice );
        return SubsystemInitResult::Succeeded;
    }

    void SceneRHISubsystem::bindAssetCaches( IRHIDevice* pDevice )
    {
        // GPU 자원을 가진 캐시가 쓸 디바이스 — 게임 스레드 요청(텍스처)과 다시 로드(텍스처 · 머티리얼 · 메시)는 디바이스를 인자로 받지 않는다
        AssetManager& assetManager = engine::getSubsystem<AssetManager>();
        assetManager.getAssetCache<TextureCache>().bindDevice( pDevice );
        assetManager.getAssetCache<MaterialCache>().bindDevice( pDevice );
        assetManager.getAssetCache<MeshCache>().bindDevice( pDevice );
    }

    void SceneRHISubsystem::shutdownSubsystem()
    {
        engine::getSubsystem<SceneManager>().setRenderBinding( nullptr );
        bindAssetCaches( nullptr );
    }

    void SceneRHISubsystem::SceneRenderBinding::flushRenderThread()
    {
        _rhiSubsystem.findRHI()->getDevice().flushRenderThread();
    }

    void SceneRHISubsystem::SceneRenderBinding::bindSceneDefaults( Scene& scene )
    {
        GameObjectManager* const pObjectManager = scene.getObjectManager();
        if ( pObjectManager == nullptr )
            return;
        Material* const pDefaultMaterial = scene.getMaterial();
        pObjectManager->forEachGameObject( SceneIterationKey{}, [&]( GameObject* pObj )
        {
            if ( pObj == nullptr )
                return;
            MeshComponent* pMeshComp = pObj->getComponent<MeshComponent>();
            if ( pMeshComp == nullptr )
                return;
            pMeshComp->resolveRenderAssets();
            // 인스턴스가 붙은 메시는 건너뛴다. 그 메시의 머티리얼은 인스턴스의 부모이고, GPUSceneBuilder 가 그렇게 고른다.
            // 여기서 씬 기본을 넣으면 배치가 기본 머티리얼(그룹, 텍스처)과 인스턴스(원소 바이트, 퍼뮤테이션)로 섞인다.
            const bool bNeedsDefaultMaterial = pMeshComp->getMaterial() == nullptr && pMeshComp->getRawMaterialInstance() == nullptr && pDefaultMaterial != nullptr;
            if ( bNeedsDefaultMaterial )
                pMeshComp->setMaterial( pDefaultMaterial );
        } );
        engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().initializePending( &_rhiSubsystem.findRHI()->getDevice() );
        pObjectManager->flushSceneTransforms();
    }

    span<const utf8* const> SceneRHISubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { RHISubsystem::kSubsystemName, SceneManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask SceneRHISubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SUBSYSTEM( SceneRHISubsystem, Startup, ( RHISubsystem ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        (void)scope.emplaceSubsystem<SceneRHISubsystem>( scope.get<RHISubsystem>() );
    } );
} // namespace sw
