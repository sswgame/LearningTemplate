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
        if ( defaultAssets.loadFromResource( path::kEngineDefaultAssets ) == false )
            SW_LOG_WARNING( "Engine data could not be read - using built-in defaults" );
        LocalizationManager& localization = engine::getLocalizationManager();
        if ( defaultAssets._cultureTable.empty() == false )
            (void)localization.loadCultureTable( defaultAssets._cultureTable ); // 실패는 loadCultureTable 이 오류로 남긴다
        if ( defaultAssets._localizationProject.empty() == false && localization.mountProject( defaultAssets._localizationProject, LocalizationScope::Engine ) == false )
            SW_LOG_ERROR( "Engine localization project '%#' is not (fully) loaded", defaultAssets._localizationProject.c_str() );
        string commandLineLanguage;
        if ( engine::getCommandLineManager().getArgument( CommandLineArgument::LANGUAGE, commandLineLanguage ) && localization.hasLanguage( commandLineLanguage ) )
            localization.setCurrentLanguage( commandLineLanguage );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> EngineDefaultAssetsSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
