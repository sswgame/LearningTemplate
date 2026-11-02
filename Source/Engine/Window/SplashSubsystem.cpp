#include "pch.h"

#include "Engine/Window/SplashSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"

namespace sw
{
    SubsystemInitResult SplashSubsystem::initializeSubsystem()
    {
        if ( engine::getSubsystem<CommandLineManager>().isArgumentProvided( CommandLineArgument::UNATTENDED ) == false )
            (void)_splash.initialize( "SW Engine", "Initializing Engine Subsystems..." ); // 스플래시 없이 기동하는 것도 정상 경로다(SplashWindow::initialize 설명)
        _splash.setProgress( 0.05f );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> SplashSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName }; // 스플래시 이미지가 에셋이다
        return kArrDependency;
    }

    SubsystemHostMask SplashSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw
