#include "pch.h"

#include "Engine/Graphics/2D/Render2DSettingsSubsystem.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/2D/Render2DSettings.h"

namespace sw
{
    Render2DSettingsSubsystem::Render2DSettingsSubsystem()
        : _settings{ make_unique<Render2DSettings>() }
    {
    }

    Render2DSettingsSubsystem::~Render2DSettingsSubsystem() = default;

    SubsystemInitResult Render2DSettingsSubsystem::initializeSubsystem()
    {
        // 정렬 레이어 표는 엔진 파일에만 있다 - 못 읽으면 대신 쓸 표가 없으므로 기동을 멈춘다(이유는 loadFromAsset 가 남긴다).
        if ( reload() == false )
        {
            SW_LOG_ERROR_ID( "Render2DSettings.DRenderSettingsCould", "2D render settings '%#' could not be read - stopping startup", Render2DSettings::getEngineSettingsPath() );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> Render2DSettingsSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName };
        return kArrDependency;
    }

    bool Render2DSettingsSubsystem::reload()
    {
        Render2DSettings engineSettings;
        if ( engineSettings.loadFromAsset( AssetID{ Render2DSettings::getEngineSettingsPath() } ) == false )
            return false;
        *_settings = engineSettings;
        return true;
    }
} // namespace sw

namespace sw
{
    // 2D 렌더 설정(정렬 레이어 · 투명 정렬 축)은 엔진 파일이다 - 마운트한 뒤에 읽는다. 2D 컴포넌트를 읽는 단계(헤드리스 쿠킹 · 렌더러)가 이 뒤에 선다.
    SW_ENGINE_SUBSYSTEM( Render2DSettingsSubsystem, Startup, (), []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        const Render2DSettingsSubsystem& render2DSettings = scope.emplaceSubsystem<Render2DSettingsSubsystem>();
        (void)scope.bind( &render2DSettings.getSettings() ); // 새 범위 — 겹치지 않는다
    } );

    // 위 공장이 건 2D 렌더 설정의 서비스 칸(읽기 전용) — 렌더러 · 2D 컴포넌트 검증 · 서버가 같이 읽는다(정적 활성본 `Render2DSettings::getActive()` 를 대신한다).
    SW_ENGINE_SERVICE( Render2DSettings, HostOnly );
} // namespace sw
