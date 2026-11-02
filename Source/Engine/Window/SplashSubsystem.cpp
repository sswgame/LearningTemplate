#include "pch.h"

#include "Engine/Window/SplashSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/HeadlessSubsystem.h"

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
        // 스플래시 이미지가 에셋이다. 헤드리스 작업(단위 테스트 · 쿠킹 · 백엔드마다 시나리오를 자식으로 돌리는 조정 프로세스)이 돌면 그 단계가 뒤를 막아(`SkipDependents`)
        // 스플래시가 서지 않는다 — 앞에 서면 조정 프로세스가 자식을 기다리는 몇 분 내내 스플래시가 떠 있었다.
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName, HeadlessSubsystem::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask SplashSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw
