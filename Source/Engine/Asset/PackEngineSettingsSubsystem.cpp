#include "pch.h"

#include "Engine/Asset/PackEngineSettingsSubsystem.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Inherit/ResourceRemap.h"

namespace sw
{
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
            SW_LOG_ERROR_ID( "PackEngineSettings.PackEngineSettingsCould", "Pack engine settings of '%#' could not be read - stopping startup", packRoot.c_str() );
            return SubsystemInitResult::Failed;
        }
        SW_LOG_INFO_ID( "PackEngineSettings.LoadedTitle", "Loaded <%#> of %# (title '%#')", PackEngineSettings::kElementName, packRoot.c_str(), _settings->_windowTitle.c_str() );
        // 덮어쓰기 층 · 경로 교체는 게임 데이터를 읽기 전에 선다(이 단계 뒤에 서는 창 · 사용자 설정 · UI · 게임이 읽는다). 틀리면 대신 쓸 것이 없으므로 기동을 멈춘다.
        if ( engine::getSubsystem<ResourceComposition>().setLayers( _settings->_listResourceLayer ) == false )
            return SubsystemInitResult::Failed;
        ResourceRemap::clear();
        if ( _settings->_resourceRemap.isSet() && ResourceRemap::loadFromAsset( AssetID{ _settings->_resourceRemap.get() } ) == false )
            return SubsystemInitResult::Failed;
        // 옮긴 에셋의 리다이렉트 표(도메인마다 `redirects.xml`) — 게임 데이터 · 세이브를 읽기 전에 선다. 틀린 표는 기동을 멈춘다.
        AssetRedirect::clear();
        if ( AssetRedirect::loadFromSearchDomains() == false )
            return SubsystemInitResult::Failed;
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> PackEngineSettingsSubsystem::getSubsystemDependencies() const
    {
        // 팩 루트는 Config 단계(GameConfig)가, 팩 마운트는 Resource 단계가 한다.
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName, AssetManager::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

namespace sw
{
    // 활성 팩의 `<Engine>` 요소(창 제목 · 스키마 · UI 규칙) — 팩을 마운트한 뒤에 읽는다. 창 · 사용자 설정 · UI 가 이 뒤에 선다.
    SW_ENGINE_SUBSYSTEM( PackEngineSettingsSubsystem, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        const PackEngineSettingsSubsystem& packSettings = scope.emplaceSubsystem<PackEngineSettingsSubsystem>();
        (void)scope.bind( &packSettings.getSettings() ); // 새 범위 — 겹치지 않는다
    } );

    // 위 공장이 건 팩 설정의 서비스 칸(읽기 전용). 엔진과 에디터가 읽는다 — 게임은 자기 `GameSettings` 를 읽는다.
    SW_ENGINE_SERVICE( PackEngineSettings, HostOnly );
} // namespace sw
