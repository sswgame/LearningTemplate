#include "pch.h"

#include "Engine/Asset/EngineDefaultAssetsSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Reflection/ReflectionSubsystem.h"

namespace sw
{
    EngineDefaultAssetsSubsystem::EngineDefaultAssetsSubsystem()
        : _defaultAssets{ make_unique<EngineDefaultAssets>( EngineDefaultAssets::ConstructKey{} ) }
    {
    }

    EngineDefaultAssetsSubsystem::~EngineDefaultAssetsSubsystem() = default;

    SubsystemInitResult EngineDefaultAssetsSubsystem::initializeSubsystem()
    {
        EngineDefaultAssets& defaultAssets = *_defaultAssets;
        // 셰이더 · 파이프라인 경로는 데이터에만 있다 — 못 읽으면 대신 쓸 값이 없으므로 기동을 멈춘다(이유는 loadFromAsset 가 남긴다).
        if ( defaultAssets.loadFromAsset( AssetID{ constant::path::kEngineDefaultAssets } ) == false )
        {
            SW_LOG_ERROR_ID( "EngineDefaultAssets.EngineDefaultAssetsCould", "Engine default assets could not be read - stopping startup" );
            return SubsystemInitResult::Failed;
        }
        // 문화권 표 · 시간대 표 · 엔진 문자열은 필수 칸이다 — 못 읽으면 숫자 · 날짜 형식과 설정 메뉴 글이 없으므로 기동을 멈춘다(이유는 각 읽기가 남긴다).
        LocalizationManager& localization = engine::getSubsystem<LocalizationManager>();
        if ( localization.loadCultureTable( AssetID{ defaultAssets._cultureTable.get() } ) == false )
        {
            SW_LOG_ERROR_ID( "EngineDefaultAssets.EngineCultureTableCould", "Engine culture table '%#' could not be read - stopping startup", defaultAssets._cultureTable.c_str() );
            return SubsystemInitResult::Failed;
        }
        if ( localization.loadTimeZoneTable( AssetID{ defaultAssets._timeZoneTable.get() } ) == false )
        {
            SW_LOG_ERROR_ID( "EngineDefaultAssets.TimeZoneTableCould", "Engine time zone table '%#' could not be read - stopping startup", defaultAssets._timeZoneTable.c_str() );
            return SubsystemInitResult::Failed;
        }
        if ( localization.mountProject( AssetID{ defaultAssets._localizationProject.get() }, LocalizationScope::Engine ) == false )
        {
            SW_LOG_ERROR_ID( "EngineDefaultAssets.EngineLocalizationProjectNot", "Engine localization project '%#' is not (fully) loaded - stopping startup", defaultAssets._localizationProject.c_str() );
            return SubsystemInitResult::Failed;
        }
        string commandLineLanguage;
        if ( engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::LANGUAGE, commandLineLanguage ) && localization.hasLanguage( commandLineLanguage ) )
            localization.setCurrentLanguage( commandLineLanguage );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> EngineDefaultAssetsSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName, ReflectionSubsystem::kSubsystemName }; // 엔진 데이터의 enum 이름 풀이가 리플렉션 바인딩 뒤다
        return kArrDependency;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SUBSYSTEM( EngineDefaultAssetsSubsystem, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        engine::StartupSetup& setup = *build._pStartupSetup;
        if ( setup._defaultAssets == nullptr )
            setup._defaultAssets = sw::make_unique<EngineDefaultAssetsSubsystem>();
        const EngineDefaultAssetsSubsystem& defaultAssets = scope.addSubsystem( std::move( setup._defaultAssets ) );
        (void)scope.bind( &defaultAssets.getDefaultAssets() ); // 새 범위 — 겹치지 않는다
    } );

    // 위 공장이 건 엔진 기본 에셋 경로 표의 서비스 칸(읽기 전용). 게임 프레임워크도 엔진 데이터(포토 모드 입력 맵 등)의 경로를 여기서 읽는다.
    SW_ENGINE_SERVICE( EngineDefaultAssets, GameVisible );
} // namespace sw
