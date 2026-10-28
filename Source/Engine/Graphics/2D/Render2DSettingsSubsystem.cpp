#include "pch.h"

#include "Engine/Graphics/2D/Render2DSettingsSubsystem.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/2D/Render2DSettings.h"

namespace sw
{
    SW_LOG_CALLER( "Render2DSettings" );

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
            SW_LOG_ERROR( "2D render settings '%#' could not be read - stopping startup", Render2DSettings::getEngineSettingsPath() );
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
