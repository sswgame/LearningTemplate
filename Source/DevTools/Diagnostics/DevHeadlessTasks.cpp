#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestRunner.h"

#include "DevTools/Diagnostics/ReflectionDocWriter.h"
#include "DevTools/Diagnostics/TestCaseStateProbe.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Facial/LipSyncImport.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/HeadlessTask.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        /** @brief 쿠킹 밖의 개발 헤드리스 작업(단위 테스트 · 백엔드마다 시나리오 · 리플렉션 문서 · 리타깃 베이크 · 립싱크 임포트)입니다. */
        struct DevHeadlessTasksInternal
        {
            /** @brief bool 인자 @p argument 가 켜져 주어졌으면 true 입니다. */
            static bool isFlagGiven( const CommandLineManager& commandLine, CommandLineArgument argument )
            {
                bool bGiven = false;
                return commandLine.getArgument( argument, bGiven ) && bGiven;
            }

            /** @brief 글 인자 @p argument 가 비지 않게 주어졌으면 true 입니다. */
            static bool isTextGiven( const CommandLineManager& commandLine, CommandLineArgument argument )
            {
                string value;
                return commandLine.getArgument( argument, value ) && value.empty() == false;
            }

            static bool isUnitTestRequested( const CommandLineManager& commandLine ) { return isFlagGiven( commandLine, CommandLineArgument::TEST ); }

            /**
             * @brief `--test` — 엔진 안 단위 테스트(`<기능>/Tests/` 의 `SW_TEST_CASE`)를 돌린다.
             * @details 모든 모듈이 올라온 뒤(ModuleLoadStage)라 엔진 · GameFramework · 키트 · 게임의 케이스가 다 등록돼 있고, 엔진 서비스가 서 있다.
             *          창 · RHI 는 세우지 않는다 — 그림이 필요한 확인은 시나리오다.
             */
            static bool runUnitTestTask( HeadlessTaskContext& context )
            {
                const CommandLineManager& commandLine = context._commandLine;
                test::TestRunOptions      options;
                int32                     seed        = 0;
                int32                     repeatCount = 1;
                (void)commandLine.getArgument( CommandLineArgument::TEST_FILTER, options._filter );
                (void)commandLine.getArgument( CommandLineArgument::TEST_SOURCE, options._sourceFolder );
                (void)commandLine.getArgument( CommandLineArgument::TEST_SEED, seed );
                (void)commandLine.getArgument( CommandLineArgument::TEST_REPEAT, repeatCount );
                (void)commandLine.getArgument( CommandLineArgument::TEST_REPORT, options._reportPath );
                bool bUpdateSnapshots        = false;
                bool bRecordMissingSnapshots = false;
                (void)commandLine.getArgument( CommandLineArgument::UPDATE_SNAPSHOTS, bUpdateSnapshots );
                (void)commandLine.getArgument( CommandLineArgument::RECORD_MISSING_SNAPSHOTS, bRecordMissingSnapshots );
                if ( bUpdateSnapshots && bRecordMissingSnapshots )
                {
                    SW_LOG_ERROR_ID( "Headless.SnapshotFlagsConflict", "--update-snapshots rewrites every snapshot and --record-missing-snapshots only the missing ones - pick one" );
                    return false;
                }
                options._snapshotWriteMode = bUpdateSnapshots        ? test::SnapshotWriteMode::All
                                           : bRecordMissingSnapshots ? test::SnapshotWriteMode::Missing
                                                                     : test::SnapshotWriteMode::Off;
                (void)commandLine.getArgument( CommandLineArgument::TEST_RUN_DISABLED, options._bRunDisabled );
                (void)commandLine.getArgument( CommandLineArgument::TEST_CHILD_ROLE, options._childRole );
                (void)commandLine.getArgument( CommandLineArgument::TEST_CHILD_ARG, options._childArgument );
                (void)commandLine.getArgument( CommandLineArgument::TEST_ISOLATED_CASE, options._isolatedCase );
                string shard{ "0/1" }; // 주지 않으면 몫 하나(전부)
                (void)commandLine.getArgument( CommandLineArgument::TEST_SHARD, shard );
                if ( test::TestRunner::parseShard( shard, options._shardIndex, options._shardCount ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.TestShardNotI", "--test-shard=%# is not i/n (0 <= i < n)", shard.c_str() );
                    return false;
                }
                options._seed          = static_cast<uint32>( seed );
                options._repeatCount   = repeatCount > 0 ? static_cast<uint32>( repeatCount ) : 1u;
                options._pCrashHandler = &engine::getSubsystem<CrashHandler>();
                TestCaseStateProbe stateProbe;
                options._onCaseBegin = SW_DELEGATE_METHOD( Delegate<void()>, &TestCaseStateProbe::beginCase, &stateProbe );
                options._onCaseEnd   = SW_DELEGATE_METHOD( Delegate<void( vector<string>& )>, &TestCaseStateProbe::endCase, &stateProbe );
                const bool bPassed   = test::TestRunner::run( options );
                // 이 실행의 "전부 통과" 를 에디터까지 통과한 것으로 읽지 않게 한다 - 에디터 모듈은 이 단계에 아직 올라오지 않았다.
                SW_LOG_INFO_ID( "Headless.EditorCasesRunElsewhere", "Editor cases are not in this run - they run inside the editor (self test editor.unitTests, ctest Scenario.editor)" );
                return bPassed;
            }

            static bool isScenarioPerBackendRequested( const CommandLineManager& commandLine ) { return isTextGiven( commandLine, CommandLineArgument::SCENARIO_BACKENDS ); }

            /**
             * @brief `-scenario=<경로> -scenario-backends=all|dx11,vk` — 시나리오를 백엔드마다 자식 App 으로 동시에 돌리고 모은다(`AutomationPerBackend`).
             * @details 일은 자동화 실행기(`IAutomationRuntime::runPerBackend`)가 한다. 실행기를 짓지 않은 구성(Shipping · 전용 서버)은 찾는 것이 없어 이유를 남기고 실패로 끝낸다(폴백 없음).
             *          `-scenario-backends` 가 없으면 보통 시나리오 실행(이 프로세스가 직접 돈다)이다.
             */
            static bool runScenarioPerBackendTask( HeadlessTaskContext& context )
            {
                IAutomationRuntime* const pAutomation = engine::findSubsystem<IAutomationRuntime>();
                if ( pAutomation == nullptr )
                {
                    SW_LOG_ERROR_ID( "Headless.ScenarioBackendsNeedsBuild", "-scenario-backends needs a development build (Debug or Release) - Shipping has no automation runner" );
                    return false;
                }
                string backends;
                (void)context._commandLine.getArgument( CommandLineArgument::SCENARIO_BACKENDS, backends );
                pAutomation->runPerBackend( backends, context._commandLine, context._state );
                return context._state._bFailed == SW_FALSE;
            }

            static bool isReflectionDocsRequested( const CommandLineManager& commandLine ) { return isTextGiven( commandLine, CommandLineArgument::WRITE_REFLECTION_DOCS ); }

            /** @brief 리플렉션 문서도 같은 자리다 — 모든 타입 공급자가 등록을 끝낸 뒤(ModuleLoadStage)라 게임 · 킷의 타입까지 든다. */
            static bool runReflectionDocsTask( HeadlessTaskContext& context )
            {
                AbsolutePath reflectionDocsDir;
                (void)context._commandLine.getArgument( CommandLineArgument::WRITE_REFLECTION_DOCS, reflectionDocsDir );
                return ReflectionDocWriter::writeMarkdown( engine::getSubsystem<TypeRegistry>(), reflectionDocsDir ) != 0;
            }

            static bool isBakeRetargetRequested( const CommandLineManager& commandLine ) { return isTextGiven( commandLine, CommandLineArgument::BAKE_RETARGET ); }

            /** @brief 리타깃 베이크(`--bake-retarget=<프로필>,<원본 클립>,<출력 클립>`) — 클립 · 스켈레톤 · 코덱이 모두 엔진 것이라 여기서 한다. */
            static bool runBakeRetargetTask( HeadlessTaskContext& context )
            {
                string bakeRetarget;
                (void)context._commandLine.getArgument( CommandLineArgument::BAKE_RETARGET, bakeRetarget );
                const string_splitter      parts( bakeRetarget, { "," } );
                const vector<string_view>& listArgument = parts.getSplitList();
                if ( listArgument.size() != 3 )
                {
                    SW_LOG_ERROR_ID( "Headless.BakeRetargetExpectsProfile", "--bake-retarget expects <profile>,<source clip>,<output clip>" );
                    return false;
                }
                return PoseRetargeter::bakeClipFile( AssetID{ StringUtil::trim( listArgument[0] ) }, AssetID{ StringUtil::trim( listArgument[1] ) }, StringUtil::trim( listArgument[2] ) );
            }

            static bool isImportLipSyncRequested( const CommandLineManager& commandLine ) { return isFlagGiven( commandLine, CommandLineArgument::IMPORT_LIPSYNC ); }

            /** @brief 립싱크 분석 — 음성마다 비즘 트랙을 곁에 쓴다. 분석 표가 엔진 데이터라 엔진이 한다(음성 디코드라 오디오를 짓지 않는 서버는 하지 않는다). */
            static bool runImportLipSyncTask( HeadlessTaskContext& context )
            {
                SW_LOG_INFO_ID( "Headless.StartingHeadlessImportLipSync", "Starting Headless (ImportLipSync)..." );
                // 분석 표는 런타임 얼굴과 같은 공유 캐시의 한 벌이다.
                const shared_ptr<const LipSyncSettings> pLipSyncSettings   = SharedAssetCache<LipSyncSettings>::acquire( engine::getSubsystem<EngineDefaultAssets>()._lipSyncSettings.get() ).snapshot();
                uint32                                  lipSyncFailedCount = 0;
                [[maybe_unused]] uint32                 lipSyncCount       = 0; // 로그로만 쓴다(Shipping 은 빠진다)
                AbsolutePath                            lipSyncOutputDir;       // 비면 음성 곁(소스 트리)
                (void)context._commandLine.getArgument( CommandLineArgument::LIPSYNC_DIR, lipSyncOutputDir );
                if ( pLipSyncSettings != nullptr )
                    lipSyncCount = LipSyncImport::importAll( engine::getSubsystem<AssetPaths>().getRootFolderPath(), lipSyncOutputDir, *pLipSyncSettings, lipSyncFailedCount );
                else
                    ++lipSyncFailedCount;
                SW_LOG_INFO_ID( "Headless.ImportedVisemeTracksFailures", "Imported %# viseme tracks (%# failures).", lipSyncCount, lipSyncFailedCount );
                return lipSyncFailedCount == 0;
            }
        };
    } // namespace

    SW_HEADLESS_TASK( unitTest, "UnitTest", HeadlessTaskPriority::kUnitTest, Any, &DevHeadlessTasksInternal::isUnitTestRequested, &DevHeadlessTasksInternal::runUnitTestTask );
    SW_HEADLESS_TASK( scenarioPerBackend, "ScenarioPerBackend", HeadlessTaskPriority::kScenarioPerBackend, Any, &DevHeadlessTasksInternal::isScenarioPerBackendRequested,
                      &DevHeadlessTasksInternal::runScenarioPerBackendTask );
    SW_HEADLESS_TASK( reflectionDocs, "ReflectionDocs", HeadlessTaskPriority::kReflectionDocs, Any, &DevHeadlessTasksInternal::isReflectionDocsRequested, &DevHeadlessTasksInternal::runReflectionDocsTask );
    SW_HEADLESS_TASK( bakeRetarget, "BakeRetarget", HeadlessTaskPriority::kBakeRetarget, Any, &DevHeadlessTasksInternal::isBakeRetargetRequested, &DevHeadlessTasksInternal::runBakeRetargetTask );
    SW_HEADLESS_TASK( importLipSync, "ImportLipSync", HeadlessTaskPriority::kImportLipSync, Client, &DevHeadlessTasksInternal::isImportLipSyncRequested, &DevHeadlessTasksInternal::runImportLipSyncTask );
} // namespace sw
