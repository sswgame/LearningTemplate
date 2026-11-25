#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/Client/Audio/AudioDumpListener.h"
#include "DevTools/Client/Automation/Scenario/AutomationScratchWorkspace.h"
#include "DevTools/Client/Automation/Scenario/AutomationUserDataRoot.h"
#include "DevTools/Client/Capture/StartupCaptureSubsystem.h"
#include "DevTools/Client/DebugHUD/DebugHUD.h"
#include "DevTools/Client/Diagnostics/AssertDialog.h"
#include "DevTools/Client/Shader/LiveShaderSubsystem.h"
#include "DevTools/Client/Shader/ShaderRecompiler.h"
#include "DevTools/Console/DevConsoleController.h"
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
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Screen/UIScreenTypes.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UI/Widgets/UIBrush.h"
#include "Engine/Visual/MeshLODSystem.h"

namespace sw
{
    /**
     * @class DevConsoleOverlay
     * @brief 게임 창 위쪽에 출력 줄과 입력 줄을 런타임 UI 오버레이로 그리는 화면 하나입니다(플랫폼 창을 띄우지 않는다 — 언리얼 `UConsole` 이 뷰포트 캔버스에 그리는 자리). 그리기만 한다 — 키보드는 `InputManager` 의 콘솔 포커스로 받는다.
     * @details 화면은 오버레이 레이어라서 포커스 · 포인터 · 게임 입력을 받지 않는다. 줄 위젯은 처음 보일 때 정해진 수만큼 만들고 글만 바꾼다.
     */
    class DevConsoleOverlay final : public IDevConsoleView
    {
    public:
        explicit DevConsoleOverlay( UISystem& ui );
        ~DevConsoleOverlay() override;
        DevConsoleOverlay( const DevConsoleOverlay& )            = delete;
        DevConsoleOverlay& operator=( const DevConsoleOverlay& ) = delete;

        /** @brief 화면을 열거나 닫습니다. */
        void setVisible( bool bVisible ) override;
        /** @brief 그릴 줄(위에서 아래로)을 넘깁니다. @p listErrorFlag 가 0 이 아닌 줄은 오류 색, 마지막 줄(입력 줄)은 입력 색으로 그립니다. */
        void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag ) override;

    private:
        UISystem*      _pUI;
        UIScreenHandle _screen; ///< 오버레이 화면입니다(닫혀 있으면 무효).
    };

    namespace
    {
        struct DevConsoleOverlayInternal
        {
            static constexpr float32 kFontSize = 14.0f;
            static constexpr float4  kPanelColor{ 0.07f, 0.08f, 0.1f, 0.92f };
            static constexpr float4  kLineColor{ 0.84f, 0.86f, 0.9f, 1.0f };
            static constexpr float4  kErrorColor{ 1.0f, 0.47f, 0.43f, 1.0f };
            static constexpr float4  kInputColor{ 1.0f, 0.86f, 0.47f, 1.0f };
            static constexpr float4  kPanelPadding{ 8.0f, 6.0f, 8.0f, 6.0f }; ///< 좌 · 위 · 우 · 아래
            static constexpr float32 kLineSpacing = 2.0f;
        };

        /**
         * @class DevConsoleScreen
         * @brief 창 위쪽 가로 전체의 패널 하나와 줄마다 글 위젯 하나입니다. 줄 수가 같으면 글과 색만 바꾼다(같은 글이면 무효화도 없다).
         */
        class DevConsoleScreen final : public UIScreen
        {
        public:
            DevConsoleScreen( const UIScreenDef& desc, unique_ptr<Widget> root, WidgetID column )
                : UIScreen{ desc, std::move( root ) }
                , _column{ column }
            {
            }

            static unique_ptr<DevConsoleScreen> create()
            {
                unique_ptr<CanvasPanel> root  = make_unique<CanvasPanel>();
                unique_ptr<BorderPanel> panel = make_unique<BorderPanel>();
                panel->setName( hashed_string( "DevConsolePanel" ) );
                panel->setBackground( UIBrush::makeSolid( DevConsoleOverlayInternal::kPanelColor, 0.0f ) );
                panel->setContentPadding( DevConsoleOverlayInternal::kPanelPadding );
                // 위 가장자리에 붙고 가로로 늘어난다. 높이는 줄 수를 따른다.
                WidgetLayoutSlot slot = panel->getLayoutSlot();
                slot._anchorMin       = float2{ 0.0f, 0.0f };
                slot._anchorMax       = float2{ 1.0f, 0.0f };
                slot._offsetMin       = float2{ 0.0f, 0.0f };
                slot._offsetMax       = float2{ 0.0f, 0.0f };
                slot._bAutoSize       = true;
                slot._growVertical    = UIGrowDirection::End;
                panel->setLayoutSlot( slot );
                unique_ptr<BoxPanel> column = make_unique<BoxPanel>();
                column->setOrientation( UIOrientation::Vertical );
                column->setSpacing( DevConsoleOverlayInternal::kLineSpacing );
                const WidgetID columnID = column->getID();
                (void)panel->addChild( std::move( column ) );
                (void)root->addChild( std::move( panel ) );

                UIScreenDef desc{};
                desc._layer       = UILayer::Overlay;
                desc._bTakesFocus = false;
                desc._bShowCursor = false;
                return sw::make_unique<DevConsoleScreen>( desc, std::move( root ), columnID );
            }

            void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag )
            {
                PanelWidget* pColumn = static_cast<PanelWidget*>( getTree().findWidgetByID( _column ) );
                if ( pColumn == nullptr )
                    return;
                const uint32 lineCount = static_cast<uint32>( listLine.size() );
                if ( pColumn->getChildCount() != lineCount )
                {
                    pColumn->clearChildren();
                    for ( uint32 index = 0; index < lineCount; ++index )
                    {
                        unique_ptr<TextWidget> text  = make_unique<TextWidget>();
                        TextLayoutStyle        style = text->getTextStyle();
                        style._fontSize              = DevConsoleOverlayInternal::kFontSize;
                        text->setTextStyle( style );
                        text->setLocalized( false ); // 개발 도구라 번역하지 않는다(명령 · 값 그대로)
                        (void)pColumn->addChild( std::move( text ) );
                    }
                }
                for ( uint32 index = 0; index < lineCount; ++index )
                {
                    TextWidget*   pText  = static_cast<TextWidget*>( pColumn->getChild( index ) );
                    const bool    bInput = index + 1 == lineCount;
                    const bool    bError = index < listErrorFlag.size() && listErrorFlag[index] != 0;
                    const float4& color  = bInput ? DevConsoleOverlayInternal::kInputColor : ( bError ? DevConsoleOverlayInternal::kErrorColor : DevConsoleOverlayInternal::kLineColor );
                    pText->setColor( color );
                    pText->setText( listLine[index] );
                }
            }

        private:
            WidgetID _column;
        };
    } // namespace

    DevConsoleOverlay::DevConsoleOverlay( UISystem& ui )
        : _pUI{ &ui }
        , _screen{ kInvalidUIScreenHandle }
    {
    }

    DevConsoleOverlay::~DevConsoleOverlay()
    {
        setVisible( false );
    }

    void DevConsoleOverlay::setVisible( bool bVisible )
    {
        if ( bVisible == ( _screen != kInvalidUIScreenHandle ) )
            return;
        if ( bVisible )
        {
            _screen = _pUI->pushScreen( DevConsoleScreen::create() );
            return;
        }
        _pUI->closeScreen( _screen );
        _screen = kInvalidUIScreenHandle;
    }

    void DevConsoleOverlay::present( const vector<string>& listLine, const vector<uint8>& listErrorFlag )
    {
        // 화면은 UI 시스템이 소유한다 — 핸들로 다시 찾는다(닫혔으면 아무것도 하지 않는다).
        if ( UIScreen* pScreen = _pUI->findScreen( _screen ) )
            static_cast<DevConsoleScreen*>( pScreen )->present( listLine, listErrorFlag );
    }

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
