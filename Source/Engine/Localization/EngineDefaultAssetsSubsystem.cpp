#include "pch.h"

#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "EngineDefaultAssets" );

    EngineDefaultAssetsSubsystem::EngineDefaultAssetsSubsystem( unique_ptr<EngineDefaultAssets> defaultAssets )
        : _defaultAssets{ std::move( defaultAssets ) }
    {
    }

    EngineDefaultAssetsSubsystem::~EngineDefaultAssetsSubsystem() = default;

    SubsystemInitResult EngineDefaultAssetsSubsystem::initializeSubsystem()
    {
        EngineDefaultAssets& defaultAssets = *_defaultAssets;
        // 엔진 기본 에셋 표와 문화권 표는 엔진 필수 데이터다. 못 읽으면 코드 값으로 계속하지 않고 기동이 실패한다.
        if ( defaultAssets.loadFromResource( path::kEngineDefaultAssets ) == false )
        {
            SW_LOG_ERROR( "Engine default assets '%#' could not be read", path::kEngineDefaultAssets );
            return SubsystemInitResult::Failed;
        }
        LocalizationManager& localization = engine::getSubsystem<LocalizationManager>();
        if ( defaultAssets._cultureTable.empty() == false && localization.loadCultureTable( defaultAssets._cultureTable ) == false )
        {
            SW_LOG_ERROR( "Engine culture table '%#' could not be loaded", defaultAssets._cultureTable.c_str() );
            return SubsystemInitResult::Failed;
        }
        if ( defaultAssets._localizationProject.empty() == false && localization.mountProject( defaultAssets._localizationProject, LocalizationScope::Engine ) == false )
            SW_LOG_ERROR( "Engine localization project '%#' is not (fully) loaded", defaultAssets._localizationProject.c_str() );
        string commandLineLanguage;
        if ( engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::LANGUAGE, commandLineLanguage ) && localization.hasLanguage( commandLineLanguage ) )
            localization.setCurrentLanguage( commandLineLanguage );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> EngineDefaultAssetsSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
