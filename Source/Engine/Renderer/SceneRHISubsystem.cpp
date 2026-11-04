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
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    SceneRHISubsystem::SceneRHISubsystem( const RHISubsystem& rhiSubsystem )
        : _rhiSubsystem{ rhiSubsystem }
    {
    }

    SubsystemInitResult SceneRHISubsystem::initializeSubsystem()
    {
        IRHIDevice* const pDevice = &_rhiSubsystem.findRHI()->getDevice();
        engine::getSubsystem<SceneManager>().setRHIDevice( pDevice );
        // GPU 자원을 가진 캐시가 쓸 디바이스 — 게임 스레드 요청(텍스처)과 다시 로드(텍스처 · 머티리얼 · 메시)는 디바이스를 인자로 받지 않는다
        AssetManager& assetManager = engine::getSubsystem<AssetManager>();
        assetManager.getAssetCache<TextureCache>().bindDevice( pDevice );
        assetManager.getAssetCache<MaterialCache>().bindDevice( pDevice );
        assetManager.getAssetCache<MeshCache>().bindDevice( pDevice );
        return SubsystemInitResult::Succeeded;
    }

    void SceneRHISubsystem::shutdownSubsystem()
    {
        engine::getSubsystem<SceneManager>().setRHIDevice( nullptr );
        AssetManager& assetManager = engine::getSubsystem<AssetManager>();
        assetManager.getAssetCache<TextureCache>().bindDevice( nullptr );
        assetManager.getAssetCache<MaterialCache>().bindDevice( nullptr );
        assetManager.getAssetCache<MeshCache>().bindDevice( nullptr );
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
    // 서브시스템 자리 등록(Engine/Foundation/EngineSubsystemRegistration.h) — 만드는 순서는 자리 표가 정한다.
    SW_ENGINE_SUBSYSTEM( SceneRHISubsystem, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        (void)scope.emplaceSubsystem<SceneRHISubsystem>( scope.get<RHISubsystem>() );
    } );
} // namespace sw
