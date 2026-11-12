#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationSession.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Client/Automation/Runner/AutomationExitCode.h"
#include "DevTools/Client/Automation/Runner/AutomationLogPolicy.h"
#include "DevTools/Client/Automation/Runner/AutomationPerBackend.h"
#include "DevTools/Client/Automation/Runner/AutomationRunner.h"
#include "DevTools/Client/Automation/Runner/AutomationScenarioChain.h"
#include "DevTools/Client/Automation/Runner/ScenarioPlayer.h"
#include "DevTools/Client/Automation/Scenario/AutomationUserDataRoot.h"

#include "Engine/Automation/Scenario/AutomationStageReset.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Engine.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/View/ScreenshotRequestQueue.h"
#include "Engine/HeadlessSubsystem.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Module/HostInterfaces.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Presentation/IEngineRenderDriver.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    namespace
    {
        /** @brief 시나리오가 첫 씬을 선언하는 `<Launch>` 변수 — 게임 프레임워크가 정의하는 이름이다(`GameInstance`). 체인의 단계는 이 씬을 새로 열고 시작한다. */
        constexpr string_view kStartSceneVariable = "gv_firstScene";
        /** @brief 단계 사이에 앞 씬을 비우는 빈 씬의 이름입니다. */
        constexpr string_view kStageResetSceneName = "AutomationStageReset";
        /** @brief 보이는 실행이 끝 결과를 남기는 기본 프레임 수입니다(60 Hz 로 1.5 초 — HUD 가 4 Hz 로 갱신하므로 몇 번은 읽힌다). */
        constexpr int32 kDefaultResultHoldFrames = 90;
    } // namespace

    /**
     * @brief `-gv_scenarioPresentInterval=<N>`: 지켜보는 시나리오(창이 보이는 실행)가 그린 프레임 N 개마다 하나만 화면에 내보낸다(1 = 매 프레임) — 시나리오 배속.
     * @details 그리기 · 사진 · 판정은 매 프레임 그대로이고 내보내기(DWM 합성 · VSync 대기)만 준다. 표시 경로를 보는 시나리오(`present="true"`)는 늘 매 프레임이다.
     *          게임 시간 배율(`gv_timeScale`)은 프레임마다 흘리는 시간을 바꿔 대기 프레임 · 골든이 달라지므로 배속으로 쓰지 않는다(언리얼 `-benchmark` · 유니티 `captureFramerate` 도 시간은 고정하고 루프만 빨리 돈다).
     */
    SW_GLOBAL_VARIABLE( int32, gv_scenarioPresentInterval, 1, "Watched scenario runs present one of every N drawn frames (1 = every frame) - scenario fast-forward" );
    /**
     * @brief `-gv_scenarioResultHoldFrames=<N>`: 창이 보이는 실행은 체인이 끝난 뒤 N 프레임 동안 끝 결과를 남기고 종료한다(0 = 바로 종료). 무인 숨김 실행은 늘 바로 끝낸다.
     * @details 판정 · 보고는 끝난 뒤라 결과를 바꾸지 않는다. 언리얼 Gauntlet 이 테스트 끝 상태를 화면에 남기고 세션을 닫는 것과 같은 자리다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_scenarioResultHoldFrames, kDefaultResultHoldFrames, "Watched scenario runs keep the final result on screen for N frames before exiting (0 = exit at once)" );

    AutomationSession::AutomationSession()
        : _pLogPolicy{ make_shared<AutomationLogPolicy>() }
        , _pRunner{ nullptr }
        , _pChain{ nullptr }
        , _recorder{}
        , _listRecordWatch{}
        , _recordPath{}
        , _reportPath{}
        , _workingFolder{}
        , _startWindowWidth{ 0 }
        , _startWindowHeight{ 0 }
        , _listStartVariable{}
        , _reproduceInput{}
        , _screenshotBaseline{ 0 }
        , _bUnattended{ SW_FALSE }
        , _runtime{ *this }
        , _runTitle{}
        , _runStatus{}
        , _heldExit{ kEngineFrameRunning }
        , _heldExitFrames{ 0 }
    {
    }

    AutomationSession::~AutomationSession() = default;

    AutomationSession::Runtime::Runtime( AutomationSession& owner )
        : _owner{ owner }
    {
    }

    AutomationSession::Runtime::~Runtime() = default;

    void AutomationSession::shutdownSubsystem()
    {
        // 실행기가 로그 리스너 · 입력을 빌려 쓴다 — 엔진 서비스가 사라지기 전에 떼고 비운다.
        _runtime.detach();
        _pRunner.reset();
        _pChain.reset();
        _recordPath.clear();
        _pRenderDriver = nullptr;
    }

    bool AutomationSession::beginRecording( string_view outputPath, string_view watchList )
    {
        if ( ScenarioRecorder::isAllowedOutputPath( outputPath ) == false )
        {
            SW_LOG_ERROR_ID( "AutomationSession.OutputMustRelativePath", "[ScenarioRecord] the output must be a relative path under Saved/Automation/ ending in .scenario.xml, got '%#'", string( outputPath ).c_str() );
            return false;
        }
        _recordPath      = string( outputPath );
        _listRecordWatch = AutomationScenarioChain::splitList( watchList );
        SW_LOG_INFO_ID( "AutomationSession.RecordingPlayCheckpoint", "[ScenarioRecord] recording the play into %# (%# checkpoint(s))", _recordPath.c_str(), static_cast<uint32>( _listRecordWatch.size() ) );
        return true;
    }

    void AutomationSession::startRecordingWhenReady()
    {
        if ( _recordPath.empty() || _recorder.isRecording() )
            return;
        Scene*                   pScene   = engine::getSubsystem<SceneManager>().getActiveScene();
        const GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        const bool               bReady   = pManager != nullptr && pManager->hasBegunPlay() && AutomationStepRegistry::isStartBlocked() == false;
        if ( bReady )
            _recorder.start();
    }

    void AutomationSession::finishRecording()
    {
        if ( _recorder.isRecording() == false )
            return;
        ScenarioRecordSettings settings;
        settings._name                         = FileUtil::removeExtension( FileUtil::removeExtension( FileUtil::getFileNamePart( _recordPath ) ) ); // a.scenario.xml → a
        settings._fixedDelta                   = _pRunner != nullptr ? _pRunner->getScenario().getFixedDelta() : settings._fixedDelta;
        settings._firstFrame                   = 0;
        settings._bIncludeSynthetic            = true;
        settings._bStartWhenScenePlaying       = true;
        const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        const IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        settings._clientWidth                  = pWindow != nullptr ? pWindow->getWidth() : 0;
        settings._clientHeight                 = pWindow != nullptr ? pWindow->getHeight() : 0;
        Scene*                   pScene        = engine::getSubsystem<SceneManager>().getActiveScene();
        const GameObjectManager* pManager      = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        for ( const string& spec : _listRecordWatch )
        {
            expected<ScenarioRecordCheckpoint> checkpoint = ScenarioRecorder::readCheckpoint( spec, pManager );
            if ( checkpoint.has_value() )
            {
                settings._listCheckpoint.push_back( std::move( *checkpoint ) );
            }
            else
            {
                SW_LOG_WARNING_ID( "AutomationSession.CheckpointSkipped", "[ScenarioRecord] checkpoint %# skipped: %#: %#", spec.c_str(), checkpoint.error().message(), checkpoint.error().detail() );
            }
        }
        const ScenarioRecordOutput output = _recorder.stop( settings );
        if ( FileUtil::ensureParentDirectoryExists( _recordPath ) == false || FileUtil::writeTextFile( _recordPath, output._xmlText ) == false )
        {
            SW_LOG_ERROR_ID( "AutomationSession.CouldNotWrite", "[ScenarioRecord] could not write %#", _recordPath.c_str() );
            return;
        }
        SW_LOG_INFO_ID( "AutomationSession.WroteStepCheckpointEvent", "[ScenarioRecord] wrote %# (%# step(s), %# checkpoint(s), %# event(s) skipped) - replay with -scenario=%#", _recordPath.c_str(),
                        output._writtenStepCount, static_cast<uint32>( settings._listCheckpoint.size() ), output._skippedEventCount,
                        ScenarioRecorder::makeReplayID( _recordPath ).c_str() );
    }

    void AutomationSession::applyPresentPolicy()
    {
        if ( _pRenderDriver == nullptr || _pRunner == nullptr )
            return;
        // 보는 사람이 없으면 내보내지 않는다(그림 · 사진은 같다). 표시 경로를 보는 시나리오는 매 프레임 내보낸다. 지켜보는 실행(-unattended 없음)은 늘 보이고
        // `gv_scenarioPresentInterval` 프레임마다 내보낸다(배속 — `begin` 이 1 이상인지 확인했다).
        const bool   bPresents       = _pRunner->getScenario().getTestOptions()._bPresents;
        const bool   bPresentSkipped = _bUnattended && bPresents == false;
        const uint32 presentInterval = bPresentSkipped ? 0u : ( bPresents ? 1u : static_cast<uint32>( gv_scenarioPresentInterval ) );
        _pRenderDriver->setPresentInterval( presentInterval );
        // 무인 실행은 창도 띄우지 않는다(App 이 띄우지 않았다 — 언리얼 -RenderOffScreen). 내보내는 단계만 메인 창을 보이고, 다음 단계가 다시 정한다.
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        if ( _bUnattended && pWindowHost != nullptr && pWindowHost->getWindow() != nullptr )
            pWindowHost->getWindow()->showWindow( bPresentSkipped == false );
    }

    bool AutomationSession::begin( string_view scenarioArgument, string_view reportPath, bool bKeepGoing )
    {
        if ( gv_scenarioPresentInterval < 1 )
        {
            SW_LOG_ERROR_ID( "AutomationSession.PresentIntervalInvalid", "[Scenario] gv_scenarioPresentInterval=%# must be 1 or more (1 = present every frame)", gv_scenarioPresentInterval );
            return false;
        }
        _reportPath    = string( reportPath );
        _workingFolder = FileUtil::getCurrentPath();
        if ( const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>(); pWindowHost != nullptr && pWindowHost->getWindow() != nullptr )
        {
            _startWindowWidth  = pWindowHost->getWindow()->getWidth();
            _startWindowHeight = pWindowHost->getWindow()->getHeight();
        }
        // 재현 명령의 재료 — 이 실행을 같은 게임 · 백엔드 · 에디터 · 시나리오 목록으로 다시 띄우는 데 필요한 것을 지금(엔진이 선 뒤) 모은다.
        _reproduceInput                   = AutomationReproduceInput{};
        _reproduceInput._game             = GameSelection::getName();
        _reproduceInput._backendName      = AutomationRunner::findBackendName();
        _reproduceInput._scenarioArgument = string( scenarioArgument );
        _reproduceInput._bKeepGoing       = bKeepGoing;
        if ( engine::areEngineServicesBound() )
        {
            const CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
            (void)commandLine.getArgument( CommandLineArgument::ENABLE_EDITOR, _reproduceInput._bEditor ); // 적지 않았으면 false 그대로
            _reproduceInput._listOriginalArgument = commandLine.getOriginalArguments();
        }
        // 녹화(`Saved/Automation/…`)를 id(`saved/…`)로 다시 돌릴 수 있게 붙인다. 못 붙이면 오류가 남고 그 id 를 읽는 단계만 읽기 오류로 진다.
        (void)ScenarioRecorder::mountSavedDomain();
        _listStartVariable.clear();
        vector<GlobalVariableInfo*> listVariable;
        engine::getSubsystem<GlobalVariableManager>().collectVariables( listVariable );
        for ( const GlobalVariableInfo* pVariable : listVariable )
            _listStartVariable.emplace_back( pVariable->_name, pVariable->getValueAsString() );
        _pChain = make_unique<AutomationScenarioChain>();
        _pChain->begin( AutomationScenarioChain::splitList( scenarioArgument ), bKeepGoing );
        if ( _pChain->isActive() == false )
        {
            _pChain.reset();
            return false;
        }
        startStage( findCompletedScreenshotCount() );
        return true;
    }

    void AutomationSession::startStage( uint32 completedScreenshotCount )
    {
        // 앞 단계가 프로세스의 작업 폴더를 옮겼으면(파일 대화상자 같은 것) 상대 경로를 쓰는 다음 단계가 엉뚱한 폴더에 쓴다 — 알리고 처음 폴더로 되돌린다.
        if ( _pChain->getStageIndex() > 0 && _workingFolder.empty() == false )
        {
            const string currentFolder = FileUtil::getCurrentPath();
            if ( currentFolder != _workingFolder )
            {
                SW_LOG_WARNING_ID( "AutomationSession.WorkingFolderChangedDuring", "[Scenario] the working folder changed to '%#' during stage %# - restoring '%#'", currentFolder.c_str(),
                                   static_cast<uint32>( _pChain->getStageIndex() ), _workingFolder.c_str() );
                (void)FileUtil::setCurrentPath( _workingFolder );
            }
        }

        const bool    bLaterStage       = _pChain->getStageIndex() > 0;
        const float64 resetStartSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
        if ( bLaterStage )
        {
            // 앞 단계의 씬을 먼저 비운다 — 변수를 되돌리고 거는 사이에 앞 씬이 다음 단계의 설정(렌더 파이프라인)으로 그려지면 둘이 맞지 않는 조합이 생긴다
            // (2D 쿠키 빛을 3D 파이프라인이 그리면 DX11 · GL 의 흉내 슬롯이 겹친다). 빈 씬은 어느 설정으로 그려도 된다.
            (void)engine::getSubsystem<SceneManager>().createEmptyActiveScene( kStageResetSceneName );
            restoreStartVariables();
            // 창 크기도 시작 때로 — 앞 단계가 바꾼 크기에서 편집기의 기본 배치를 세우면 도크 칸이 새 프로세스와 화소 몇 개 달라 골든이 어긋났다(씬 뷰 160x64 대 62).
            restoreStartWindowSize();
            // 엔진이 모르는 모듈 상태(편집기의 패널 · 팝업 · 배치)도 새 프로세스처럼 — 그 상태를 가진 모듈이 등록한 되돌리기를 부른다.
            (void)AutomationStageResetRegistry::resetAll();
            // 앞 단계가 찾지 못한 번역 키(시네마틱 자막 같은 것)도 비운다 — 판정의 로그 게이트가 그 목록을 세므로, 남기면 뒤 단계가 모두 그 키로 진다.
            engine::getSubsystem<LocalizationManager>().clearMissingKeys();
            // 단계 사이 초기화가 든 시간이다 - 다음 시나리오가 시작하기까지의 프레임(새 게임 · 첫 씬)은 그 실행의 timeline `start` 가 센다.
            SW_LOG_INFO_ID( "AutomationSession.StageResetTookMs", "[Scenario] stage reset took %#ms", static_cast<uint32>( ( static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 - resetStartSeconds ) * 1000.0 ) );
        }

        _pRunner = make_unique<AutomationRunner>( _pLogPolicy );
        _pRunner->setReproduceInput( _reproduceInput );
        _pRunner->armHangWatchdog(); // 실제 프로세스를 구동하는 세션만 행 감시를 켠다(상한 + 유예 뒤 보고를 쓰고 종료 코드 12 로 끝낸다)
        _pRunner->startFromPath( AssetID{ _pChain->getCurrentPath() }, AutomationScenarioChain::makeStageReportPath( _reportPath, _pChain->getStageIndex(), _pChain->getStageCount() ) );
        _screenshotBaseline = completedScreenshotCount;
        applyPresentPolicy();
        if ( _pRunner->isActive() )
        {
            // 단계의 `<Launch>` 값을 건다(첫 단계는 명령줄로 이미 같다). 실행 중에 걸어도 다 듣는다 — 렌더 파이프라인은 다시 고르고(`FrameRendererSubsystem`),
            // 씬이 세울 때 읽는 값(스트레스 씬 · 첫 씬)은 아래에서 씬을 새로 열 때 읽힌다. 실행기의 `<Launch>` 확인(`prepare`)이 걸렸는지 본다.
            GlobalVariableManager& globalVariables = engine::getSubsystem<GlobalVariableManager>();
            string                 startScene;
            for ( const AutomationAttribute& variable : _pRunner->getScenario().getTestOptions()._listLaunchVariable )
            {
                GlobalVariableInfo* const pVariable = globalVariables.findVariable( variable._name );
                if ( pVariable == nullptr )
                    continue; // 모르는 변수는 `prepare` 가 읽기 오류로 낸다
                if ( bLaterStage == false )
                    removeStartValue( variable._name, pVariable->getDefaultValueAsString() );
                if ( pVariable->getValueAsString() != variable._value )
                    (void)pVariable->setValueFromString( variable._value );
                if ( variable._name == kStartSceneVariable )
                    startScene = variable._value;
            }
            // 결정적 실행 정책 — 애니메이터가 시작 클립을 기다리고(첫 포즈가 디스크 지연에 달리지 않게), 에셋 도착을 프레임에 묶는다(끄면 셰이더 캐시가 빈 첫 실행 —
            // 느린 프레임 — 이 다른 그림을 낸다). 게임이 서기 전에 건다(첫 단계는 게임이 이 뒤에 서고, 다음 단계는 아래에서 다시 선다). 환경 시간 멈춤은 시나리오가 고른다.
            DeterministicRunPolicy policy     = engine::getSubsystem<DeterministicRun>().getPolicy();
            policy._bWaitForAnimationClips    = SW_TRUE;
            policy._bFinishAssetJobsEachFrame = SW_TRUE;
            policy._fixedFrameDelta           = _pRunner->getScenario().getFixedDelta(); // 시나리오 프레임 시간 — App 이 벽시계 대신 흘린다
            engine::getSubsystem<DeterministicRun>().setPolicy( policy );
            // 둘째 단계부터는 게임을 처음부터 다시 세운다 — 새 게임 인스턴스가 기동 때처럼 첫 씬(`gv_firstScene` > 타이틀 > 시작 맵)을 열거나 벤치를 세운다.
            // 그래서 단계는 첫 단계와 같은 길로 선다(첫 씬을 선언하지 않은 단계도 체인에 든다). 재시작할 호스트가 없으면 앞 단계의 상태로 돌지 않고 멈춘다.
            if ( bLaterStage )
            {
                IGameRestarter* const pRestarter = engine::findSubsystem<IGameRestarter>();
                if ( pRestarter == nullptr || pRestarter->restartGame() == false )
                {
                    _pRunner->finish( AutomationResult::Failed,
                                      pRestarter == nullptr ? "this host cannot restart the game for a later chain stage" : "the game could not be restarted for this stage" );
                    return;
                }
            }
            if ( startScene.empty() == false )
                _pRunner->requireStartScene( startScene );
        }
    }

    void AutomationSession::removeStartValue( string_view name, string_view value )
    {
        for ( auto& [startName, startValue] : _listStartVariable )
        {
            if ( startName == name )
                startValue = string( value );
        }
    }

    void AutomationSession::restoreStartWindowSize()
    {
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>(); // 선택: 창 없는 하니스에는 없다
        IWindow* const             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pWindow == nullptr || _startWindowWidth == 0 || ( pWindow->getWidth() == _startWindowWidth && pWindow->getHeight() == _startWindowHeight ) )
            return;
        if ( pWindow->setDisplayMode( WindowDisplayMode::Windowed, _startWindowWidth, _startWindowHeight ) == false )
            SW_LOG_WARNING_ID( "AutomationSession.WindowCouldNotReturn", "[Scenario] the window could not return to its start size %#x%# for the next stage", _startWindowWidth, _startWindowHeight );
    }

    void AutomationSession::restoreStartVariables()
    {
        GlobalVariableManager& globalVariables = engine::getSubsystem<GlobalVariableManager>();
        for ( const auto& [name, value] : _listStartVariable )
        {
            GlobalVariableInfo* const pVariable = globalVariables.findVariable( name );
            if ( pVariable == nullptr )
                continue;
            const string current = pVariable->getValueAsString();
            if ( current == value )
                continue;
            if ( pVariable->setValueFromString( value ) )
                SW_LOG_INFO_ID( "AutomationSession.RestoredNextStage", "[Scenario] %# restored '%#' -> '%#' for the next stage", name.c_str(), current.c_str(), value.c_str() );
            else
                SW_LOG_WARNING_ID( "AutomationSession.CouldNotRestoredNext", "[Scenario] %# could not be restored to '%#' for the next stage", name.c_str(), value.c_str() );
        }
    }

    EngineFrameHooks AutomationSession::makeFrameHooks()
    {
        EngineFrameHooks hooks{};
        hooks._onFrameBegin     = SW_DELEGATE_METHOD( EngineFrameDelegate, &AutomationSession::onFrameBegin, this );
        hooks._onAfterSceneTick = SW_DELEGATE_METHOD( EngineFrameDelegate, &AutomationSession::onAfterSceneTick, this );
        hooks._onFrameEnd       = SW_DELEGATE_METHOD( EngineFrameExitDelegate, &AutomationSession::onFrameEnd, this );
        hooks._onWindowClosed   = SW_DELEGATE_METHOD( EngineFrameExitDelegate, &AutomationSession::onWindowClosed, this );
        return hooks;
    }

    void AutomationSession::onFrameBegin( const EngineFrameContext& context )
    {
        startRecordingWhenReady();
        if ( _pRunner != nullptr && context._pInput != nullptr )
            _pRunner->onFrameBegin( *context._pInput );
    }

    EngineFrameExit AutomationSession::onFrameEnd( const EngineFrameContext& context )
    {
        updateRunStatus();
        updateRunTitle();
        if ( _heldExit.has_value() )
        {
            // 끝 결과를 남기는 프레임 — 판정 · 보고는 끝났다. 다 세면 쥔 코드를 낸다.
            if ( _heldExitFrames > 0 )
                --_heldExitFrames;
            return _heldExitFrames == 0 ? _heldExit : kEngineFrameRunning;
        }
        // 녹화는 시나리오보다 먼저 이 프레임을 적는다 — 시나리오가 이 프레임에 끝나도 마지막 프레임이 녹화에 포함된다.
        if ( _recorder.isRecording() && context._pInput != nullptr )
            _recorder.update( *context._pInput, context._deltaSeconds );
        // `isActive` 가 아니라 `hasEnded` — 시작 프레임에서 진 시나리오(읽기 오류 · 시작 시한)도 여기서 끝맺고 그 코드로 끝난다.
        const bool bNoRunnerToUpdate = _pRunner == nullptr || _pRunner->hasEnded() || context._pInput == nullptr;
        if ( bNoRunnerToUpdate )
            return kEngineFrameRunning;
        const uint32 completedScreenshotCount = findCompletedScreenshotCount();
        _pRunner->setCompletedScreenshotCount( completedScreenshotCount >= _screenshotBaseline ? completedScreenshotCount - _screenshotBaseline : 0u );
        // 사진을 실은 패킷은 방금 제출했고 렌더 스레드가 아직 그리는 중일 수 있다. 이제 돌 단계가 렌더 스레드가 그릴 때 읽는 전역 변수(gv_upscaler …)를 바꾸면
        // 그 값이 사진에 섞인다 — 백엔드마다 렌더 스레드가 뒤처지는 정도가 달라 Vulkan · GL 만 FSR 이 섞였다(useroptions scale). 단계 앞에서 패킷을 비운다(언리얼 FlushRenderingCommands).
        // 시계를 붙잡지 않는다 — 붙잡으면 게임이 그동안 더 돌아 뒤 사진의 게임 시간이 렌더 지연에 따라 흔들린다.
        if ( _pRenderDriver != nullptr && _pRunner->hasPendingScreenshot() )
            _pRenderDriver->flushSubmittedPackets();
        const AutomationResult result = _pRunner->onFrameEnd( *context._pInput );
        if ( result == AutomationResult::Running )
            return kEngineFrameRunning;
        const EngineFrameExit exitCode = onStageEnded( result, completedScreenshotCount );
        if ( exitCode.has_value() == false )
            return kEngineFrameRunning;
        finishRecording();
        return holdFinalExit( exitCode );
    }

    EngineFrameExit AutomationSession::holdFinalExit( EngineFrameExit exitCode )
    {
        // 보이는 실행만 남긴다 — 무인 숨김 실행(`_bUnattended`)과 창 없는 호스트(렌더 구동기 없음)는 바로 끝낸다.
        const bool bWatched = _bUnattended == SW_FALSE && _pRenderDriver != nullptr && gv_scenarioResultHoldFrames > 0;
        if ( bWatched == false )
            return exitCode;
        updateRunStatus(); // 끝난 프레임의 결과를 바로 싣는다
        _heldExit       = exitCode;
        _heldExitFrames = static_cast<uint32>( gv_scenarioResultHoldFrames );
        return kEngineFrameRunning;
    }

    uint32 AutomationSession::findCompletedScreenshotCount()
    {
        const ScreenshotRequestQueue* const pMailbox = engine::findSubsystem<ScreenshotRequestQueue>();
        return pMailbox != nullptr ? pMailbox->getCompletedPacketSerial() : 0u;
    }

    void AutomationSession::updateRunTitle()
    {
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        const RHI* const           pRHI        = _pRenderDriver != nullptr ? _pRenderDriver->findRHI() : nullptr;
        const utf8* const          pBackend    = pRHI != nullptr && pRHI->hasDevice() ? RHI::getBackendTypeName( pRHI->getCommittedBackend() ) : "";
        const bool                 bRunning    = _pRunner != nullptr && _pRunner->isActive();
        _runTitle.onFrameEnd( pWindowHost != nullptr ? pWindowHost->getTitleTarget() : nullptr, engine::findSubsystem<CommandLineManager>(), pBackend,
                              bRunning ? &_runStatus : nullptr );
    }

    void AutomationSession::updateRunStatus()
    {
        if ( _pRunner == nullptr )
            return;
        const AutomationRunner& runner = *_pRunner;
        _runStatus._scenarioName       = runner.getScenario().getName();
        _runStatus._stepKind           = runner.getCurrentStepKind();
        _runStatus._stepNumber         = runner.getCurrentStepNumber();
        _runStatus._frame              = runner.getFrameIndex();
        _runStatus._result             = runner.getResult();
        _runStatus._stageIndex         = _pChain != nullptr ? static_cast<uint32>( _pChain->getStageIndex() ) : 0u;
        _runStatus._stageCount         = _pChain != nullptr ? static_cast<uint32>( _pChain->getStageCount() ) : 1u;
        const vector<string>& listFail = runner.getFailures();
        _runStatus._failureCount       = static_cast<uint32>( listFail.size() );
        if ( listFail.empty() )
            _runStatus._firstFailure.clear();
        else if ( _runStatus._firstFailure != listFail.front() )
            _runStatus._firstFailure = listFail.front();
        // 끝맺은 뒤에는 로그 게이트가 판정한 줄 수, 그 전에는 모은 Warning · Error 수(허용 판정 전)다.
        _runStatus._warningErrorCount = runner.hasEnded() ? static_cast<uint32>( runner.getUnhandledLogs().size() ) : runner.getCapturedWarningErrorCount();
    }

    EngineFrameExit AutomationSession::onStageEnded( AutomationResult result, uint32 completedScreenshotCount )
    {
        const size_t stageIndex = _pChain->getStageIndex();
        const string stageName  = _pRunner->getScenario().getName().empty() ? _pChain->getCurrentPath() : _pRunner->getScenario().getName();
        const bool   bNextStage = _pChain->onStageEnded( result );
        if ( _pChain->isChain() )
        {
            const string line = _pChain->describeStageEnd( stageIndex, result, stageName );
            // 건너뜀은 실패가 아니다 - 까닭과 등급(빌드 구성이면 Info, 환경 탓이면 Warning)은 실행기의 SKIP 줄이 이미 남겼다.
            if ( result == AutomationResult::Passed || result == AutomationResult::Skipped )
                SW_LOG_INFO_ID( "AutomationSession.ChainStageEnded", "[Scenario] %#", line.c_str() );
            else
                SW_LOG_ERROR_ID( "AutomationSession.ChainStageFailed", "[Scenario] %#", line.c_str() );
        }
        if ( bNextStage )
        {
            _runStatus._previousStageName   = stageName; // 다음 단계가 곧 서므로 끝난 단계의 결과는 "앞 단계" 줄로 남긴다
            _runStatus._previousStageResult = result;
            startStage( completedScreenshotCount );
            return kEngineFrameRunning;
        }
        if ( _pChain->isChain() )
            SW_LOG_INFO_ID( "AutomationSession.ChainStageFailedSkipped", "[Scenario] chain %# (%# stage(s), %# failed, %# skipped, %# not run)", getEnumName( _pChain->getFinalResult() ),
                            static_cast<uint32>( _pChain->getStageCount() ), static_cast<uint32>( _pChain->getFailedStageCount() ),
                            static_cast<uint32>( _pChain->getSkippedResultCount() ), static_cast<uint32>( _pChain->getSkippedStageCount() ) );
        return getAutomationExitCode( _pChain->getFinalResult() );
    }

    EngineFrameExit AutomationSession::onWindowClosed( const EngineFrameContext& context )
    {
        finishRecording();
        // 끝 결과를 남기던 중에 창이 닫혔다 — 쥔 체인 결과를 그대로 낸다.
        if ( _heldExit.has_value() )
            return _heldExit;
        // 이미 끝난 세션은 정하지 않는다 — 종료 코드는 끝낸 프레임(`onStageEnded`)이 체인 전체의 결과로 정했다. 여기서 러너의 결과를 돌려주면
        // 마지막 단계의 결과(통과 0)가 앞 단계의 실패(10)를 덮는다(엔진 루프는 창 닫힘 관찰자의 코드를 앞선 종료 요청보다 앞세운다).
        if ( _pRunner == nullptr || _pRunner->hasEnded() )
            return kEngineFrameRunning;
        AutomationResult result = _pRunner->onWindowClosed( context._pInput );
        // 창이 닫히면 루프가 끝난다 — 돌릴 단계가 남아 있으면 체인은 끝까지 가지 못했다(`CloseWindow` 는 마지막 단계에만 둔다).
        if ( _pChain != nullptr && _pChain->onStageEnded( result ) )
        {
            SW_LOG_ERROR_ID( "AutomationSession.WindowClosedStageStill", "[Scenario] the window closed with %# stage(s) still to run (put <CloseWindow> in the last scenario of a chain)",
                             static_cast<uint32>( _pChain->getStageCount() - _pChain->getStageIndex() ) );
            result = AutomationResult::Failed;
        }
        return getAutomationExitCode( result );
    }

    void AutomationSession::onAfterSceneTick( [[maybe_unused]] const EngineFrameContext& context )
    {
        if ( _pRunner != nullptr )
            _pRunner->forwardPendingScreenshot();
    }

    // 실행기는 스스로 엔진 범위에 선다 — 개발 도구 이미지의 등록도 호스트 라이브러리 걷기(`engine::registerHostLibraryTypes`)가 "Engine" 이름으로 걷는다. 이 폴더를 짓지 않는 구성(배포 · 전용 서버)은 등록이 없어 호스트가 찾지 못한다(`IAutomationRuntime`).
    SW_MODULE_SUBSYSTEM( Engine, AutomationSession, IAutomationRuntime );

    bool AutomationSession::Runtime::start( Engine& engine, SubsystemScope& host, const CommandLineManager& commandLine )
    {
        string recordPath;
        string recordProbes;
        if ( commandLine.getArgument( CommandLineArgument::SCENARIO_RECORD, recordPath ) && recordPath.empty() == false )
        {
            (void)commandLine.getArgument( CommandLineArgument::SCENARIO_RECORD_PROBES, recordProbes ); // 없으면 검사점 없이 입력만 녹화한다
            (void)_owner.beginRecording( recordPath, recordProbes );                                    // 거절하면 이유를 로그로 남기고 녹화 없이 돈다
        }

        string scenarioPath;
        string reportPath;
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_REPORT, reportPath ); // 없으면 보고 파일을 쓰지 않는다
        IEngineRenderDriver* const pRenderDriver = engine.getRenderDriver();
        const bool                 bKeepGoing    = commandLine.isArgumentProvided( CommandLineArgument::SCENARIO_KEEP_GOING );
        _owner._bUnattended                      = MainWindowSubsystem::shouldRenderOffScreen( commandLine ); // 지켜보는 무인 실행은 보이고 내보낸다
        const bool bScenario                     = commandLine.getArgument( CommandLineArgument::SCENARIO, scenarioPath ) && scenarioPath.empty() == false;
        const bool bStarted                      = bScenario && _owner.begin( scenarioPath, reportPath, bKeepGoing );
        if ( bStarted && pRenderDriver != nullptr )
            pRenderDriver->setScenarioCaptureEnabled( true );
        _owner._pRenderDriver = pRenderDriver;
        if ( bStarted )
            _owner.applyPresentPolicy(); // 첫 단계는 렌더 스레드를 받기 전에 시작했다 — 다음 단계는 시작할 때 다시 정한다

        // 늘 붙인다 — 녹화(`-scenario-record`)만 도는 실행도 프레임을 본다. 쉬는 세션의 프레임 훅은 아무것도 하지 않는다.
        _frameSubscription = engine.getFrameHooks().add( _owner.makeFrameHooks() );
        // 기본 재생기는 세션과 따로 선 객체다 — 자기 프레임 훅을 들고, 호스트 범위에 인터페이스로 건다(`ScenarioPlayback` 이 범위에서 찾는다, 정적 칸을 두지 않는다).
        _pPlayer                = make_unique<ScenarioPlayer>( engine.getFrameHooks(), _owner._pLogPolicy );
        const bool bPlayerBound = host.bind<IScenarioPlayer>( _pPlayer.get() );
        SW_ASSERT( bPlayerBound );
        (void)bPlayerBound;
        _pHost = &host;
        return true;
    }

    const AutomationRunStatus* AutomationSession::Runtime::findRunStatus() const
    {
        // 끝난 단계의 실행기도 남는다(마지막 단계 · 결과를 남기는 프레임) — 그동안은 끝난 상태를 준다. 녹화만 도는 실행은 시나리오가 없다.
        return _owner._pRunner != nullptr ? &_owner._runStatus : nullptr;
    }

    bool AutomationSession::Runtime::isBusy() const
    {
        // 재생(데모 · 벤치마크 경로 — `ScenarioPlayer`)은 사람이 보는 화면이라 보통 프레임 속도로 돈다 — 세션의 무인 실행만 센다.
        return _owner.isBusy();
    }

    void AutomationSession::Runtime::detach()
    {
        if ( _pHost == nullptr )
            return;
        _pHost->unbind<IScenarioPlayer>(); // 재생기를 내리기 전에 푼다
        _pHost = nullptr;
        _pPlayer.reset();
        _owner._runTitle.reset();
        _frameSubscription.reset();
    }

    void AutomationSession::Runtime::runPerBackend( string_view backends, CommandLineManager& commandLine, HeadlessTaskState& inoutState )
    {
        // 창 · RHI 를 세우기 전에 가로챈다 — 이 프로세스는 기다리기만 한다.
        inoutState.enterHeadless();
        AutomationPerBackendOptions options;
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO, options._scenarioPath );
        (void)commandLine.getArgument( CommandLineArgument::LOG_FILE, options._logFilePath );
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_REPORT, options._reportPath );
        options._executablePath          = FileUtil::getExecutablePath();
        options._listPassThroughArgument = AutomationPerBackend::filterPassThroughArguments( commandLine.getOriginalArguments() );
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_BACKEND_SPLIT_OFFSET, options._splitOffset );
        (void)commandLine.getArgument( CommandLineArgument::SCENARIO_BACKEND_SLOTS, options._slotCount );
        expected<AutomationBackendSplit> split = AutomationBackendSplit::None;
        string                           splitText;
        if ( commandLine.getArgument( CommandLineArgument::SCENARIO_BACKEND_SPLIT, splitText ) )
            split = AutomationPerBackend::parseBackendSplit( splitText );
        if ( split.has_value() )
            options._split = *split;
        const expected<vector<string>> listBackend = AutomationPerBackend::parseBackendList( backends );
        if ( listBackend.has_value() )
            options._listBackend = *listBackend;
        if ( options._scenarioPath.empty() || listBackend.has_value() == false || split.has_value() == false )
        {
            if ( options._scenarioPath.empty() )
            {
                SW_LOG_ERROR_ID( "AutomationSession.BackendsOptionInvalid", "%#", "-scenario-backends needs -scenario=<path>" );
            }
            else if ( listBackend.has_value() == false )
            {
                SW_LOG_ERROR_ID( "AutomationSession.BackendsOptionInvalid", "%#: %#", listBackend.error().message(), listBackend.error().detail() );
            }
            else
            {
                SW_LOG_ERROR_ID( "AutomationSession.BackendsOptionInvalid", "%#: %#", split.error().message(), split.error().detail() );
            }
            inoutState._bFailed         = SW_TRUE;
            inoutState._failureExitCode = getAutomationExitStatus( AutomationResult::LoadError );
            return;
        }
        // 부모의 뿌리(기동 단계 `AutomationUserDataIsolation` 이 이미 정했으면 같은 값) — 자식마다 그 아래 제 폴더를 건다. 자동화 폴더 밖이면 자식을 띄우지 않는다.
        expected<string> userDataRoot = AutomationUserDataRoot::resolve( AutomationUserDataRoot::isOutsideAllowed() );
        if ( userDataRoot.has_value() == false )
        {
            SW_LOG_ERROR_ID( "AutomationUserData.RootRejected", "[Automation] %#: %#", userDataRoot.error().message(), userDataRoot.error().detail() );
            inoutState._bFailed         = SW_TRUE;
            inoutState._failureExitCode = getAutomationExitStatus( AutomationResult::LoadError );
            return;
        }
        // 상태 자리 변수(`-gv_editorStateDir` 등)를 그대로 넘기면 자식 모두가 한 자리를 함께 쓴다 — 사용자 데이터 뿌리와 같은 결함이라 띄우지 않는다.
        if ( const optional<string> shared = AutomationUserDataRoot::findSharedStateArgument( options._listPassThroughArgument ); shared.has_value() )
        {
            SW_LOG_ERROR_ID( "AutomationUserData.SharedStateArgument", "[Scenario] -scenario-backends would hand '%#' to every backend child - they would share one state folder; drop it",
                             shared->c_str() );
            inoutState._bFailed         = SW_TRUE;
            inoutState._failureExitCode = getAutomationExitStatus( AutomationResult::LoadError );
            return;
        }
        options._userDataRoot       = std::move( *userDataRoot );
        const int32 exitCode        = AutomationPerBackend::run( options );
        inoutState._bFailed         = exitCode != 0;
        inoutState._failureExitCode = exitCode;
    }
} // namespace sw
