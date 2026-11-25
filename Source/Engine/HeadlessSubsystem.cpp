#include "pch.h"

#include "Engine/HeadlessSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Logger.h"
#include "Core/Process/StandardOutput.h"
#include "Core/Subsystem/SubsystemCollection.h"

#include "Engine/Asset/EngineDefaultAssetsSubsystem.h"
#include "Engine/Foundation/Commandlet.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/2D/Render2DSettingsSubsystem.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Module/ModuleLoadStageSubsystem.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Text/FontService.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Window/SplashSubsystem.h"

namespace sw
{
    namespace
    {
        struct HeadlessSubsystemInternal
        {
            /** @brief bool 인자 @p argument 가 켜져 주어졌으면 true 입니다. */
            static bool isFlagGiven( const CommandLineManager& commandLine, CommandLineArgument argument )
            {
                bool bGiven = false;
                return commandLine.getArgument( argument, bGiven ) && bGiven;
            }

            static bool isHelpRequested( const CommandLineManager& commandLine ) { return isFlagGiven( commandLine, CommandLineArgument::HELP ); }

            /** @brief `-help` — 받는 인자 목록을 표준 출력에 찍고 끝낸다(창 · 렌더러를 세우지 않는다). */
            static bool runHelpTask( CommandletContext& context )
            {
                // 로거를 거치지 않는다 — 도움말은 로그 줄(시각 · 범주 머리)이 아니라 그대로 읽을 글이고, 로그 수준을 낮춘 실행에서도 나와야 한다.
                if ( StandardOutput::write( context._commandLine.makeUsageText() ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.CouldNotWriteHelp", "Could not write the -help text to standard output" );
                    return false;
                }
                return true;
            }
        };
    } // namespace

    // 배포에도 있는 헤드리스 작업. 쿠킹 · 테스트 · 문서 · 에디터 모듈 작업 같은 개발 작업은 DevTools 가 자기 자리에서 올린다(`DevTools/Cook/CookCommandlets.cpp` · `DevTools/Diagnostics/DevCommandlets.cpp` ·
    // `DevTools/Host/EditorModuleCommandlets.cpp`).
    using HeadlessInternal = HeadlessSubsystemInternal;
    SW_COMMANDLET( help, "Help", CommandletPriority::kHelp, Any, &HeadlessInternal::isHelpRequested, &HeadlessInternal::runHelpTask );
} // namespace sw

namespace sw
{
    HeadlessSubsystem::HeadlessSubsystem( bool bDedicatedServer, CommandletState& outState )
        : _state{ outState }
        , _bDedicatedServer{ bDedicatedServer }
    {
    }

    SubsystemInitResult HeadlessSubsystem::initializeSubsystem()
    {
        // 작업(테스트 · 쿠킹 · 도구)의 조회는 이 서브시스템의 의존이 아니다 — 초기화 조회 검사에서 뺀다(`SubsystemCollection::HostedCommandletScope`).
        const SubsystemCollection::HostedCommandletScope hostedTask;
        // 작업 하나가 돌았거나 거절됐으면 이 단계에 기대는 단계(창 · RHI · 렌더러)를 건너뛴다. 고르는 규칙은 `CommandletRegistration::selectAndRun`.
        CommandletContext      context{ engine::getSubsystem<CommandLineManager>(), _state, _bDedicatedServer == SW_TRUE };
        const CommandletResult result = CommandletRegistration::runRequested( context );
        return result == CommandletResult::NotRequested ? SubsystemInitResult::Succeeded : SubsystemInitResult::SkipDependents;
    }

    span<const utf8* const> HeadlessSubsystem::getSubsystemDependencies() const
    {
        // 2D 렌더 설정은 씬 쿠킹의 컴포넌트 검증(정렬 레이어 이름)이 읽는다.
        static constexpr const utf8* kArrDependency[] = { EngineDefaultAssetsSubsystem::kSubsystemName, ModuleLoadStageSubsystem::kSubsystemName, Render2DSettingsSubsystem::kSubsystemName,
                                                          SceneManager::kSubsystemName, ShaderCache::kSubsystemName };
        return kArrDependency;
    }

    span<const utf8* const> HeadlessSubsystem::getSubsystemDependents() const
    {
        // 스플래시도 막는다 — 창을 띄우지 않는 작업(단위 테스트 · 쿠킹 · 백엔드마다 시나리오를 자식으로 돌리는 조정 프로세스)에 스플래시가 서면 작업 내내 갱신되지 않은 채 떠 있었다.
        static constexpr const utf8* kArrDependent[] = { FontService::kSubsystemName, kRHISubsystemName, UserSettingsManager::kSubsystemName, SplashSubsystem::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw

namespace sw
{
    // 헤드리스 작업(셰이더 · 씬 쿠킹, 임포트)이면 그에 기대는 단계(창 · RHI · 렌더러)를 건너뛴다 — 결과는 호스트가 준 상태에 적는다.
    SW_ENGINE_SUBSYSTEM( HeadlessSubsystem, Process, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        const engine::StartupSetup& setup = *build._pStartupSetup;
        (void)scope.emplaceSubsystem<HeadlessSubsystem>( setup._bDedicatedServer, setup._headlessState );
    } );
} // namespace sw
