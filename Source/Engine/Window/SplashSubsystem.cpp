#include "pch.h"

#include "Engine/Window/SplashSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    SubsystemInitResult SplashSubsystem::initializeSubsystem()
    {
        // 이미지는 에디터 도메인 에셋이고 에디터 팩은 만들지 않는다 — 팩만 읽는 실행(배포 · `-cooked-content`)은 띄우지 않는다. 무인 실행도 보는 사람이 없어 띄우지 않는다.
        const bool bCookedContent = AssetIO::getContentSource() == ContentSource::Cooked;
        const bool bUnattended    = engine::getSubsystem<CommandLineManager>().isArgumentProvided( CommandLineArgument::UNATTENDED );
        if ( bCookedContent == false && bUnattended == false )
            (void)_splash.initialize( AssetID{ engine::getSubsystem<EngineConfig>()._splashImage.get() }, "SW Engine", "Initializing Engine Subsystems..." ); // 스플래시 없이 기동하는 것도 정상 경로다(SplashWindow::initialize 설명)
        _splash.setProgress( 0.05f );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> SplashSubsystem::getSubsystemDependencies() const
    {
        // 스플래시 이미지가 에셋이다. 헤드리스 판정 뒤에 서는 것은 헤드리스 단계가 이것을 자기 뒤로 적어 정한다(`HeadlessSubsystem::getSubsystemDependents` — 층이 위다).
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask SplashSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw

namespace sw
{
    // 에셋 · 헤드리스 판정 뒤에 스플래시를 띄운다 — 엔진 초기화(RHI 디바이스 생성)를 기다리지 않는다.
    // 창 없는 전용 서버 역할은 창 · RHI · 렌더러를 두지 않는다(`StartupSetup::_bDedicatedServer`) — 만들지 않는다.
    SW_ENGINE_SUBSYSTEM( SplashSubsystem, Process, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bDedicatedServer )
            return;
        (void)scope.emplaceSubsystem<SplashSubsystem>();
    } );
} // namespace sw
