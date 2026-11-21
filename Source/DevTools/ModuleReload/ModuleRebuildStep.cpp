/**
 * @file ModuleRebuildStep.cpp
 * @brief 시나리오 단계 `<RebuildModule name="SWGame" variant="…"/>` — 판 머리를 판 글로 덮고(원본은 실행기에 맡긴다) 모듈을 다시 지어 갈아 끼웁니다.
 * @details 모듈 리로드 폴더라 정적 모듈 구성은 짓지 않는다(그 구성에는 이 단계가 없다). 판 표 읽기는 `AutomationReloadSteps::parseVariant`, 빌드와 교체 예약은
 *          `ModuleReloadDriver::queueRebuild`, 끝 기다리기는 감시 값 `Module.ReloadPending` 이다. 빌드 도구를 부르는 단계라 단위 테스트는 판 표 읽기만 본다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "DevTools/AssetReload/AutomationReloadSteps.h"
#include "DevTools/ModuleReload/ModuleReloadDriver.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/IAutomationStepContext.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {
        struct ModuleRebuildStepInternal
        {
            /** @brief 빌드 트리(바이너리 폴더의 부모 — `ModuleCompiler` 가 `cmake --build` 를 부르는 자리)입니다. 없으면(패키지 · 복사한 바이너리) 빈 글입니다. */
            static string findBuildDirectory()
            {
                const string parentDir  = FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( ProcessRoots::get()._binary.str() ) );
                const bool   bBuildTree = FileUtil::isRegularFile( FileUtil::toDiskPath( FileUtil::joinPath( parentDir, "build.ninja" ) ) ) || FileUtil::isRegularFile( FileUtil::toDiskPath( FileUtil::joinPath( parentDir, "CMakeCache.txt" ) ) );
                return bBuildTree ? parentDir : string{};
            }

            /** @brief 판 머리를 판 글로 덮습니다(원본은 실행기에 맡긴다 — 시나리오 끝에 되돌린다). 실패하면 그 까닭입니다. */
            static expected<void> applyVariant( IAutomationStepContext& context, string_view variantName )
            {
                const string variantPath = FileUtil::joinPath( ProcessRoots::get()._project.str(), AutomationReloadSteps::kVariantFile );
                string       tableText;
                if ( FileUtil::readTextFile( FileUtil::toDiskPath( variantPath ), tableText ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "the module rebuild variant table could not be read", variantPath } );
                const expected<ModuleRebuildVariant> variant = AutomationReloadSteps::parseVariant( tableText, variantPath, variantName );
                if ( variant.has_value() == false )
                    return unexpected( variant.error() );
                const string buildDir = findBuildDirectory();
                if ( buildDir.empty() )
                    return unexpected( Error{ ErrorKind::Unsupported, "no CMake build tree next to the binary folder - this run cannot rebuild modules", ProcessRoots::get()._binary.str() } );
                // 판 머리는 CMake 가 기본 글로 만든다(추적 소스를 고치지 않으려고 빌드 트리에 둔다) — 없으면 판을 받는 소스가 없는 빌드다. 지어낸 자리에 쓰지 않는다.
                const string headerPath = FileUtil::joinPath( FileUtil::joinPath( buildDir, "generated" ), variant->_header );
                string       original;
                if ( FileUtil::isRegularFile( FileUtil::toDiskPath( headerPath ) ) == false || FileUtil::readTextFile( FileUtil::toDiskPath( headerPath ), original ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "the variant header is not in the build tree (does CMake generate it for this module?)", headerPath } );
                context.restoreFileAtEnd( headerPath, std::move( original ) );
                if ( FileUtil::writeTextFile( FileUtil::toDiskPath( headerPath ), variant->_text ) == false )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "could not write the variant header", headerPath } );
                return {};
            }

            static bool runRebuildModule( IAutomationStepContext& context, const AutomationStep& step )
            {
                const AutomationRebuildModuleParams params  = step.readValidatedProperties<AutomationRebuildModuleParams>();
                ModuleReloadDriver* const           pDriver = engine::findSubsystem<ModuleReloadDriver>();
                if ( pDriver == nullptr || pDriver->getLiveReloadManager() == nullptr )
                {
                    context.recordFailure( step, "this host has no module reload driver - RebuildModule needs a dynamic-module dev build" );
                    return true;
                }
                if ( params._variant.empty() == false )
                {
                    const expected<void> applied = applyVariant( context, params._variant );
                    if ( applied.has_value() == false )
                    {
                        fixed_string<constant::buffer::kMax1024> message;
                        formatstring( message.data(), message.capacity(), "%#: %#", applied.error().message(), applied.error().detail() );
                        context.recordFailure( step, message.view() );
                        return true;
                    }
                }
                // 빌드는 컴파일러 스레드가 돌고, 끝은 `Module.ReloadPending`(벽시계 감시 값)으로 기다린다 — 빌드 시간은 구성 · 기계에 달려 프레임으로 셀 수 없다.
                if ( pDriver->queueRebuild( params._name.c_str() ) == false )
                {
                    fixed_string<constant::buffer::kMax256> message;
                    formatstring( message.data(), message.capacity(), "module '%#' is not loaded, or a module build is already running", params._name );
                    context.recordFailure( step, message.view() );
                }
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( reloadRebuildModule, "RebuildModule", &ModuleRebuildStepInternal::runRebuildModule, &AutomationStep::validateProperties<AutomationRebuildModuleParams>,
                        AutomationStepOptions{} );
} // namespace sw
