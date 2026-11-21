/**
 * @file AutomationReloadSteps.cpp
 * @brief 플레이 중 리로드 단계 `OverwriteConfig` · `RestoreOverwrites` 와 판 표 읽기를 구현합니다. `RebuildModule` 은 모듈 리로드가 있는 구성만 짓는 `ModuleReload/ModuleRebuildStep.cpp` 에 있다.
 */
#include "pch.h"

#include "DevTools/AssetReload/AutomationReloadSteps.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/IAutomationStepContext.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct AutomationReloadStepsInternal
        {
            /** @brief 글 연산으로 판정하는 상대 경로인지 봅니다 — 비지 않았고, 절대 경로가 아니며, `..` 마디가 없다. */
            static bool isPlainRelativePath( string_view path )
            {
                if ( path.empty() || AbsolutePath::isStrictAbsolute( path ) || path.front() == '/' || path.front() == '\\' )
                    return false;
                StringBuilder<constant::buffer::kMax1024> wrapped;
                wrapped.appendFormat( "/%#/", FileUtil::normalizeSeparators( path ) );
                return wrapped.view().find( "/../" ) == string_view::npos;
            }

            static expected<void> validateOverwriteConfig( const AutomationStep& step )
            {
                AutomationOverwriteConfigParams params;
                const expected<void>            read = step.readProperties( params );
                if ( read.has_value() == false )
                    return read;
                return AutomationReloadSteps::validateConfigFile( params._configFile );
            }

            static bool runOverwriteConfig( IAutomationStepContext& context, const AutomationStep& step )
            {
                const AutomationOverwriteConfigParams params     = step.readValidatedProperties<AutomationOverwriteConfigParams>();
                const AbsolutePath                    configPath = ProcessRoots::get()._project.join( "Config" ).join( params._configFile );
                string                                original;
                if ( FileUtil::readTextFile( configPath, original ) == false )
                {
                    fixed_string<constant::buffer::kMax2048> message;
                    formatstring( message.data(), message.capacity(), "no config file '%#' to overwrite", configPath.str() );
                    context.recordFailure( step, message.view() );
                    return true;
                }
                string text = original; // from 이 비면 바꾸지 않은 저장이다 — 감시가 같은 글을 다시 읽는다
                if ( params._from.empty() == false )
                {
                    const optional<AbsolutePath> sourcePath = engine::getSubsystem<AssetPaths>().findLooseFile( params._from );
                    if ( sourcePath.has_value() == false || FileUtil::readTextFile( *sourcePath, text ) == false )
                    {
                        fixed_string<constant::buffer::kMax2048> message;
                        formatstring( message.data(), message.capacity(), "no resource '%#' to copy over the config", params._from );
                        context.recordFailure( step, message.view() );
                        return true;
                    }
                }
                context.restoreFileAtEnd( configPath.str(), std::move( original ) );
                // 쓰기 게이트가 허락한 폴더(`Config/Editor`)만 써진다 — 그 밖은 게이트가 까닭을 남기고 쓰기가 진다.
                if ( FileUtil::writeTextFile( configPath, text ) == false )
                {
                    fixed_string<constant::buffer::kMax2048> message;
                    formatstring( message.data(), message.capacity(), "could not write '%#' (the automation write gate opens only Config/Editor)", configPath.str() );
                    context.recordFailure( step, message.view() );
                }
                return true;
            }

            static bool runRestoreFiles( IAutomationStepContext& context, const AutomationStep& step )
            {
                if ( context.restoreFilesNow() == false )
                    context.recordFailure( step, "a file overwritten by this scenario could not be restored - see Automation.RestoreFileFailed" );
                return true;
            }
        };
    } // namespace

    expected<ModuleRebuildVariant> AutomationReloadSteps::parseVariant( string_view jsonText, string_view sourceName, string_view variantName )
    {
        JSONDocument document;
        if ( document.parse( jsonText, sourceName ) == false || document.getRoot().isObject() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the module rebuild variant table is not a JSON object", sourceName } );
        const JSONValue variants = document.getRoot().get( "_variants" );
        const JSONValue entry    = variants.isObject() ? variants.get( variantName ) : JSONValue{};
        if ( entry.isObject() == false )
            return unexpected( Error{ ErrorKind::NotFound, "the module rebuild variant table has no such variant under _variants", variantName } );
        const optional<string> header = entry.readRequired<string>( "_header" );
        const optional<string> text   = entry.readRequired<string>( "_text" );
        if ( header.has_value() == false || text.has_value() == false || text->empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a module rebuild variant needs _header and a non-empty _text", variantName } );
        if ( AutomationReloadStepsInternal::isPlainRelativePath( *header ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a module rebuild variant _header is a relative path under the build tree's generated/ folder", *header } );
        return ModuleRebuildVariant{ *header, *text };
    }

    bool AutomationReloadSteps::isPlainRelativePath( string_view path )
    {
        return AutomationReloadStepsInternal::isPlainRelativePath( path );
    }

    expected<void> AutomationReloadSteps::validateConfigFile( string_view file )
    {
        if ( AutomationReloadStepsInternal::isPlainRelativePath( file ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "OverwriteConfig configFile is a relative path under Config/ (no '..', not absolute)", file } );
        if ( FileUtil::hasExtension( file, ".json" ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "OverwriteConfig configFile is a .json config", file } );
        return {};
    }

    SW_AUTOMATION_STEP( reloadOverwriteConfig, "OverwriteConfig", &AutomationReloadStepsInternal::runOverwriteConfig, &AutomationReloadStepsInternal::validateOverwriteConfig,
                        AutomationStepOptions{} );
    SW_AUTOMATION_STEP( reloadRestoreOverwrites, "RestoreOverwrites", &AutomationReloadStepsInternal::runRestoreFiles, &AutomationStep::validateProperties<AutomationNoParams>,
                        AutomationStepOptions{} );
} // namespace sw
