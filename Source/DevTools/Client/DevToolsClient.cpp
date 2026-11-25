#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/Client/Audio/AudioDumpListener.h"
#include "DevTools/Client/Automation/Scenario/AutomationScratchWorkspace.h"
#include "DevTools/Client/Automation/Scenario/AutomationUserDataRoot.h"
#include "DevTools/Client/Capture/StartupCaptureSubsystem.h"
#include "DevTools/Client/Console/DevConsoleOverlay.h"
#include "DevTools/Client/DebugHUD/DebugHUD.h"
#include "DevTools/Client/Diagnostics/AssertDialog.h"
#include "DevTools/Client/Shader/LiveShaderSubsystem.h"
#include "DevTools/Client/Shader/ShaderRecompiler.h"
#include "DevTools/DevToolsClientPart.h"
#include "DevTools/Diagnostics/CrashInjection.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Visual/MeshLODSystem.h"

namespace sw
{
    /** @brief `-gv_meshLodForce=N` — 메시 LOD 를 화면 크기와 상관없이 단계 N 으로 그립니다(-1 이면 화면 크기로 고른다). 활성 씬에만 건다(에디터 미리보기 씬은 자기 고정을 쓴다). 언리얼 `r.ForceLOD` 자리. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_meshLodForce, MeshLODSystem::kPickByScreenSize, "Draw every LOD mesh of the active scene at this LOD level regardless of screen size (-1 = pick by screen size)" );
    /** @brief `-gv_autoRenderScaleForce=S` — 자동 렌더 배율을 S(0 초과 1 이하)로 고정합니다. 0 이면 끔(조정기 경로). 시나리오 game/playground/automation/renderscalestep 이 쓴다. 언리얼 `r.DynamicRes.TestScreenPercentage` 자리. */
    SW_TEST_GLOBAL_VARIABLE( float32, gv_autoRenderScaleForce, 0.0f,
                             "Pin the auto render scale of the main view to this value (0..1] regardless of GPU time - deterministic scale steps for scenarios and measurement, 0 = off" );
    /**
     * @brief `-gv_crashTest=N`: 기동 때(렌더러 뒤) 일부러 크래시를 냅니다(리포트 경로 검증용). N 은 `CrashInjectionKind` 입니다.
     * @details 크래시 리포트 경로를 실제로 확인하는 유일한 방법이다(리포트는 크래시가 나야만 만들어진다).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_crashTest, 0, "일부러 크래시를 내 리포트 경로를 검증합니다 (1=널 쓰기 2=스택 오버플로 3=작업 스레드 스택 오버플로 4=abort 5=순수 가상 호출)" );

    namespace
    {
        /**
         * @class CrashInjectionSubsystem
         * @brief `-gv_crashTest=N` 이면 기동 때 일부러 죽습니다(`CrashInjectionKind`). 렌더러(RHI) 뒤에 서므로 백엔드 이름이 크래시 문맥에 들어간 뒤다.
         * @details 런타임 `RHISubsystem` 이 이 변수를 읽지 않도록 개발 도구 클라이언트가 붙인다.
         */
        class CrashInjectionSubsystem final : public Subsystem
        {
        public:
            static constexpr const utf8* kSubsystemName      = "CrashInjection";
            static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::EngineMisc;

            SubsystemInitResult initializeSubsystem() override
            {
                if ( gv_crashTest != 0 )
                    CrashInjector::crash( static_cast<CrashInjectionKind>( gv_crashTest ) );
                return SubsystemInitResult::Succeeded;
            }
            void                    shutdownSubsystem() override {}
            const utf8*             getSubsystemName() const override { return kSubsystemName; }
            span<const utf8* const> getSubsystemDependencies() const override
            {
                static constexpr const utf8* kArrDependency[] = { RHISubsystem::kSubsystemName };
                return kArrDependency;
            }
            SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        };

        struct DevToolsClientInternal
        {
            /**
             * @brief `-gv_meshLodForce` 를 활성 씬의 LOD 시스템에 맞춥니다 — 런타임 고정 창구(`MeshLODSystem::setForcedLOD`, 메시 미리보기 창이 쓰는 길)를 부른다.
             * @details 건 고정은 활성 씬의 LOD 시스템이 든다 — 그 칸을 거는 곳은 이 스위치뿐이라 "지난 프레임에 걸었나" 를 따로 두지 않고 -1 로 돌아오면 풀기만 한다(씬이 바뀌면 새 씬에 다시 건다).
             */
            static void applyMeshLODForce()
            {
                const int32              forcedLOD     = gv_meshLodForce;
                SceneManager* const      pSceneManager = engine::findSubsystem<SceneManager>();
                Scene* const             pActiveScene  = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                GameObjectManager* const pManager      = pActiveScene != nullptr ? pActiveScene->getObjectManager() : nullptr;
                if ( pManager == nullptr )
                    return;
                MeshLODSystem& lodSystem = pManager->getSubsystem<MeshLODSystem>();
                const int32    wantedLOD = forcedLOD < 0 ? MeshLODSystem::kPickByScreenSize : forcedLOD;
                if ( lodSystem.getForcedLOD() != wantedLOD )
                    lodSystem.setForcedLOD( wantedLOD );
            }

            /** @brief `-gv_autoRenderScaleForce` 를 주 렌더러의 패킷 빌더에 겁니다(`RenderPacketBuilder::setForcedAutoRenderScale`). 0 으로 돌아오면 고정을 풀고, 렌더러가 없는 실행은 아무것도 하지 않는다. */
            static void applyAutoRenderScaleForce()
            {
                FrameRendererSubsystem* const pSubsystem = engine::findSubsystem<FrameRendererSubsystem>();
                if ( pSubsystem == nullptr )
                    return;
                const float32 forced = static_cast<float32>( gv_autoRenderScaleForce );
                pSubsystem->getPacketBuilder().setForcedAutoRenderScale( forced > 0.0f ? forced : 0.0f );
            }
        };
        /**
         * @brief 개발 도구의 클라이언트 전용 부분입니다 — 셰이더 라이브 리로드(렌더러) · 시작 인자 사진(렌더러) · 디버그 HUD(런타임 UI) · 콘솔 오버레이(런타임 UI).
         * @details 세운 것은 기동 범위가 든다 — 이 객체의 상태는 단언 대화상자를 한 번 확인했는지뿐이다. 전용 서버는 이 소스를 짓지 않는다(`IDevToolsClientPart`).
         */
        class DevToolsClient final : public IDevToolsClientPart
        {
        public:
            DevToolsClient()
                : _bAssertDialogChecked{ SW_FALSE }
                , _reserved{ 0 }
            {
            }

            void addStartupSubsystems( SubsystemScope& startup ) override
            {
                (void)startup.emplaceSubsystem<AudioDumpBuffer>();          // `-gv_audioDumpPath` 의 쌓은 렌더
                (void)startup.emplaceSubsystem<AutomationScratchCounter>(); // 시나리오 사본을 끝까지 만든 횟수(세대 번호)
                // `-scenario` 실행의 사용자 데이터 뿌리를 자동화 폴더 아래로 정한다 — 호스트 서비스(게임 · 로컬 저장)가 뿌리를 읽기 전이다. 밖이면 기동을 멈춘다(종료 11).
                (void)startup.emplaceSubsystem<AutomationUserDataIsolation>();
                // 셰이더 라이브 리로드는 렌더러가 있는 호스트만(렌더러를 끈 기동은 렌더 쪽이 nullptr).
                FrameRendererSubsystem* const pFrameRenderer = startup.find<FrameRendererSubsystem>();
                RenderThreadSubsystem* const  pRenderThread  = startup.find<RenderThreadSubsystem>();
                if ( pFrameRenderer != nullptr && pRenderThread != nullptr )
                {
                    (void)startup.emplaceSubsystem<LiveShaderSubsystem>( *pFrameRenderer, *pRenderThread );
                    (void)startup.emplaceSubsystem<CrashInjectionSubsystem>(); // `-gv_crashTest` — 렌더러 뒤에 죽어 백엔드 이름까지 보고에 든다
                    (void)startup.emplaceSubsystem<StartupCaptureSubsystem>(); // `-gv_screenshot` · `-gv_screenshotQuit` — 렌더 스레드의 실행 중 요청 창구로 찍는다
                }
                // 디버그 HUD 는 런타임 UI 위에 그린다 — UI 가 있는 실행만.
                if ( UISystem* const pUISystem = startup.find<UISystem>() )
                    (void)startup.emplaceSubsystem<DebugHUD>( *pUISystem, startup.find<UserSettingsManager>() );
            }

            void onAfterSceneTick( InputMap* pShellMap ) override
            {
                DevToolsClientInternal::applyMeshLODForce();         // `-gv_meshLodForce` — 다음 프레임의 LOD 선택부터 든다
                DevToolsClientInternal::applyAutoRenderScaleForce(); // `-gv_autoRenderScaleForce` — 이번 프레임 패킷의 자동 렌더 배율부터 든다
                if ( DebugHUD* const pDebugHUD = engine::findSubsystem<DebugHUD>() )
                    pDebugHUD->update( pShellMap );
            }

            EngineFrameExit onFrameEnd() override
            {
                // 대화형 에디터 실행의 단언 대화상자는 첫 프레임 끝에 한 번 건다 — 모든 모듈(자동 실행을 알리는 에디터 자체 테스트 변수가 에디터 모듈에 있다)이 오른 뒤다.
                // 예전에는 App 이 개발 폴더(`App/Dev`)를 거쳐 걸었다 — 개발 기능이 스스로 붙는다(docs/agents/CppStyle.md "Build configuration switches" 셋째 줄).
                if ( _bAssertDialogChecked == SW_FALSE )
                {
                    _bAssertDialogChecked                 = SW_TRUE;
                    const CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
                    bool                      bEditor     = false;
                    if ( commandLine.getArgument( CommandLineArgument::ENABLE_EDITOR, bEditor ) && bEditor )
                        AssertDialog::installForInteractiveRun( commandLine );
                }
                if constexpr ( build::kDebug )
                {
                    // 셰이더 재컴파일 · 강제 리로드 단축키(`ReloadShaders`)는 Debug 클라이언트만 — 단축키의 뜻은 재컴파일기를 가진 이쪽이 읽는다(디버그 키 맵은 입력 사실만 낸다).
                    LiveShaderSubsystem* const pLiveShader = engine::findSubsystem<LiveShaderSubsystem>();
                    if ( ShaderRecompiler* const pRecompiler = pLiveShader != nullptr ? pLiveShader->findShaderRecompiler() : nullptr )
                    {
                        pRecompiler->update();
                        if ( engine::getSubsystem<DebugKeyActions>().wasActionTriggered( hashed_string( InputMapDefaults::kReloadShadersAction ) ) )
                        {
                            pRecompiler->triggerReloadAll();
                            SW_LOG_INFO_ID( "DevTools.ForceShaderReload", "%#: force shader reload", InputMapDefaults::kReloadShadersAction );
                        }
                    }
                }
                // 시작 인자 사진(`-gv_screenshot`)의 차례 · 결과와 `-gv_screenshotQuit` 의 종료 — 렌더러가 있는 실행만 섰다.
                StartupCaptureSubsystem* const pStartupCapture = engine::findSubsystem<StartupCaptureSubsystem>();
                return pStartupCapture != nullptr ? pStartupCapture->update() : kEngineFrameRunning;
            }

            unique_ptr<IDevConsoleView> createConsoleView() override
            {
                UISystem* const pUI = engine::findSubsystem<UISystem>();
                return pUI != nullptr ? make_unique<DevConsoleOverlay>( *pUI ) : nullptr;
            }

        private:
            uint8                  _bAssertDialogChecked : 1; ///< 단언 대화상자를 걸지 한 번 확인했다(`onFrameEnd`)
            [[maybe_unused]] uint8 _reserved             : 7;
        };

        DevToolsClient                          s_devToolsClient;
        const IDevToolsClientPart::Registration s_devToolsClientRegistration{ s_devToolsClient };
    } // namespace
} // namespace sw
