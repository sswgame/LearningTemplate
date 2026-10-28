#include "pch.h"

#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Reflection/ReflectionSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "EngineDefaultAssets" );

    EngineDefaultAssetsSubsystem::EngineDefaultAssetsSubsystem()
        : _defaultAssets{ make_unique<EngineDefaultAssets>( EngineDefaultAssets::ConstructKey{} ) }
    {
    }

    EngineDefaultAssetsSubsystem::~EngineDefaultAssetsSubsystem() = default;

    SubsystemInitResult EngineDefaultAssetsSubsystem::initializeSubsystem()
    {
        EngineDefaultAssets& defaultAssets = *_defaultAssets;
        // 셰이더 · 파이프라인 경로는 데이터에만 있다 — 못 읽으면 대신 쓸 값이 없으므로 기동을 멈춘다(이유는 loadFromAsset 가 남긴다).
        if ( defaultAssets.loadFromAsset( AssetID{ path::kEngineDefaultAssets } ) == false )
        {
            SW_LOG_ERROR( "Engine default assets could not be read - stopping startup" );
            return SubsystemInitResult::Failed;
        }
        // 문화권 표 · 엔진 문자열은 필수 칸이다 — 못 읽으면 숫자 · 날짜 형식과 설정 메뉴 글이 없으므로 기동을 멈춘다(이유는 각 읽기가 남긴다).
        LocalizationManager& localization = engine::getSubsystem<LocalizationManager>();
        if ( localization.loadCultureTable( AssetID{ defaultAssets._cultureTable.get() } ) == false )
        {
            SW_LOG_ERROR( "Engine culture table '%#' could not be read - stopping startup", defaultAssets._cultureTable.c_str() );
            return SubsystemInitResult::Failed;
        }
        if ( localization.mountProject( AssetID{ defaultAssets._localizationProject.get() }, LocalizationScope::Engine ) == false )
        {
            SW_LOG_ERROR( "Engine localization project '%#' is not (fully) loaded - stopping startup", defaultAssets._localizationProject.c_str() );
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
