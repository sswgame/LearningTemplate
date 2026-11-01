#include "pch.h"

#include "Engine/Renderer/SceneRHISubsystem.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
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
        engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().bindDevice( pDevice ); // 게임 스레드 텍스처 요청이 올릴 디바이스
        return SubsystemInitResult::Succeeded;
    }

    void SceneRHISubsystem::shutdownSubsystem()
    {
        engine::getSubsystem<SceneManager>().setRHIDevice( nullptr );
        engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().bindDevice( nullptr );
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
