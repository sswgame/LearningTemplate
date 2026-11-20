#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/Client/Audio/AudioDumpListener.h"
#include "DevTools/Client/Automation/Scenario/AutomationScratchWorkspace.h"
#include "DevTools/Client/Automation/Scenario/AutomationUserDataRoot.h"
#include "DevTools/Client/Capture/StartupCaptureSubsystem.h"
#include "DevTools/Client/Console/DevConsoleOverlay.h"
#include "DevTools/Client/DebugHUD/DebugHUD.h"
#include "DevTools/Client/Diagnostics/AssertDialog.h"
#include "DevTools/Client/Diagnostics/CrashInjectionSubsystem.h"
#include "DevTools/Client/Render/AutoRenderScaleForce.h"
#include "DevTools/Client/Render/MeshLODForce.h"
#include "DevTools/Client/Shader/LiveShaderSubsystem.h"
#include "DevTools/Client/Shader/ShaderRecompiler.h"
#include "DevTools/DevToolsClientPart.h"

#include "Engine/EngineSubsystemSet.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/UI/UISystem.h"

namespace sw
{
    namespace
    {
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

            void addStartupSubsystems( SubsystemScope& startup, const engine::StartupParts& parts ) override
            {
                (void)startup.emplaceSubsystem<AudioDumpBuffer>();          // `-gv_audioDumpPath` 의 쌓은 렌더
                (void)startup.emplaceSubsystem<AutomationScratchCounter>(); // 시나리오 사본을 끝까지 만든 횟수(세대 번호)
                // `-scenario` 실행의 사용자 데이터 뿌리를 자동화 폴더 아래로 정한다 — 호스트 서비스(게임 · 로컬 저장)가 뿌리를 읽기 전이다. 밖이면 기동을 멈춘다(종료 11).
                (void)startup.emplaceSubsystem<AutomationUserDataIsolation>();
                // 셰이더 라이브 리로드는 렌더러가 있는 호스트만(렌더러를 끈 기동은 렌더 쪽이 nullptr).
                if ( parts._pFrameRendererSubsystem != nullptr && parts._pRenderThread != nullptr )
                {
                    (void)startup.emplaceSubsystem<LiveShaderSubsystem>( *parts._pFrameRendererSubsystem, *parts._pRenderThread );
                    (void)startup.emplaceSubsystem<CrashInjectionSubsystem>(); // `-gv_crashTest` — 렌더러 뒤에 죽어 백엔드 이름까지 보고에 든다
                    (void)startup.emplaceSubsystem<StartupCaptureSubsystem>(); // `-gv_screenshot` · `-gv_screenshotQuit` — 렌더 스레드의 실행 중 요청 창구로 찍는다
                }
                // 디버그 HUD 는 런타임 UI 위에 그린다 — UI 가 있는 실행만.
                if ( parts._pUISystem != nullptr )
                    (void)startup.emplaceSubsystem<DebugHUD>( *parts._pUISystem, parts._pUserSettings );
            }

            void onAfterSceneTick( InputMap* pShellMap ) override
            {
                MeshLODForce::apply();         // `-gv_meshLodForce` — 다음 프레임의 LOD 선택부터 든다
                AutoRenderScaleForce::apply(); // `-gv_autoRenderScaleForce` — 이번 프레임 패킷의 자동 렌더 배율부터 든다
                if ( DebugHUD* const pDebugHUD = engine::findSubsystem<DebugHUD>() )
                    pDebugHUD->update( pShellMap );
            }

            EngineFrameExit onFrameEnd() override
            {
                // 대화형 에디터 실행의 단언 대화상자는 첫 프레임 끝에 한 번 건다 — 모든 모듈(자동 실행을 알리는 에디터 자체 테스트 변수가 에디터 모듈에 있다)이 오른 뒤다.
                // 예전에는 App 이 개발 폴더(`App/Dev/InteractiveAssertDialog`)를 거쳐 걸었다 — 개발 기능이 스스로 붙는다(docs/agents/BuildConfigSwitch.md 셋째 줄).
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
