#include "pch.h"

#include "Engine/Asset/PackEngineSettingsSubsystem.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "PackEngineSettings" );
} // namespace sw

namespace sw
{
    PackEngineSettingsSubsystem::PackEngineSettingsSubsystem()
        : _settings{ make_unique<PackEngineSettings>() }
    {
    }

    PackEngineSettingsSubsystem::~PackEngineSettingsSubsystem() = default;

    SubsystemInitResult PackEngineSettingsSubsystem::initializeSubsystem()
    {
        // 창 제목 · 스키마 · UI 규칙은 팩에만 있다 — 못 읽으면 대신 쓸 값이 없으므로 기동을 멈춘다(이유는 loadFromPack 이 남긴다).
        const DataPath& packRoot = engine::getSubsystem<GameConfig>()._packRoot;
        if ( _settings->loadFromPack( packRoot ) == false )
        {
            SW_LOG_ERROR( "Pack engine settings of '%#' could not be read - stopping startup", packRoot.c_str() );
            return SubsystemInitResult::Failed;
        }
        SW_LOG_INFO( "Loaded <%#> of %# (title '%#')", PackEngineSettings::kElementName, packRoot.c_str(), _settings->_windowTitle.c_str() );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> PackEngineSettingsSubsystem::getSubsystemDependencies() const
    {
        // 팩 루트는 Config 단계(GameConfig)가, 팩 마운트는 Resource 단계가 한다.
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName, AssetManager::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
