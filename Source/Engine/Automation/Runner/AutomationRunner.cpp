#include "pch.h"

#include "Engine/Automation/Runner/AutomationRunner.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Automation/AutomationRunTitle.h"
#include "Engine/Automation/Runner/AutomationExitCode.h"
#include "Engine/Automation/Runner/AutomationImageMetric.h"
#include "Engine/Automation/Runner/AutomationStepArgs.h"
#include "Engine/Automation/Scenario/AutomationEnvironmentSteps.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/AutomationStepValidation.h"
#include "Engine/Automation/Scenario/AutomationWindowSteps.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Module/HostServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    namespace
    {
        struct AutomationRunnerInternal
        {
            /**
             * @brief 이 실행의 산출물 폴더(`Saved/Automation/<시나리오>/<프로세스>`)입니다 — 사진 · 녹화가 여기 쌓인다.
             * @details 실행마다 새 폴더다 - 만들기가 곧 자리 잡기(`reserveUniquePath`)다. 예전에는 시나리오 이름만이라 나란히 도는 실행(백엔드 넷)이 같은 사진을 덮어썼고,
             *          그다음엔 프로세스 id 폴더를 그대로 써 Windows 가 다시 쓴 id 의 옛 산출물 위에 이어 썼다.
             */
            static string makeOutputDirectory( string_view scenarioName )
            {
                return FileUtil::reserveUniquePath( formatToString( "%#/%#/%#", makeAutomationOutputFolderPath(), string( scenarioName ), Process::getCurrentProcessID() ), {}, true );
            }

            /** @brief 단조 시계의 지금 시각(초)입니다. 벽시계 감시와 시한이 같은 시계를 읽는다. */
            [[nodiscard]] static float64 readNowSeconds() { return static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6; }

            /**
             * @brief 이 구성에 없는 단계가 들어 있는 시나리오면 그 첫 단계를 반환합니다. 개발 명령(`DevCommand`)은 Shipping 에서 빠집니다.
             * @details 그런 시나리오는 이 구성에서 돌 수 없을 뿐 틀린 것이 아니다. 읽기 오류(11)가 아니라 건너뜀(13)으로 처리한다
             *          (언리얼 자동화 테스트가 구성에 없는 문맥의 테스트를 목록에서 빼는 것과 같다).
             */
            static const AutomationStep* findStepMissingFromThisBuild( [[maybe_unused]] const AutomationScenario& scenario )
            {
#if SW_DEV_COMMANDS_ENABLED
                return nullptr;
#else
                for ( const AutomationStep& step : scenario.getSteps() )
                {
                    if ( string_view{ step._kind } == AutomationEnvironmentSteps::kDevCommandKind )
                        return &step;
                }
                return nullptr;
#endif
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AutomationRunner::AutomationRunner()
        : _scenario{}
        , _inputScript{}
        , _judge{}
        , _run{}
        , _reportWriter{}
        , _wallProfile{}
        , _outputDirectory{}
        , _finishReason{}
        , _startedAt{}
        , _reproduceInput{}
        , _hangPayload{}
        , _hangWatchdog{}
        , _pObjectManagerOverride{ nullptr }
        , _pInputManager{ nullptr }
        , _startedUnixMs{ 0 }
        , _startScenePath{}
        , _startSceneLoad{}
        , _result{ AutomationOutcome::Running }
        , _bStarted{ SW_FALSE }
        , _bEnded{ SW_FALSE }
        , _bHangWatchArmed{ SW_FALSE }
        , _bWriteGateHeld{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    // 로그 리스너 해제는 `_judge` 가 가진 `AutomationLogCapture` 의 소멸자가 맡는다.
    AutomationRunner::~AutomationRunner()
    {
        releaseWriteGate();
    }

    void AutomationRunner::acquireWriteGate()
    {
        if ( _bWriteGateHeld == SW_TRUE )
            return;
        AutomationScratch::acquireWriteGate();
        _bWriteGateHeld = SW_TRUE;
    }

    void AutomationRunner::releaseWriteGate()
    {
        if ( _bWriteGateHeld == SW_FALSE )
            return;
        AutomationScratch::releaseWriteGate();
        _bWriteGateHeld = SW_FALSE;
    }

    void AutomationRunner::startFromPath( const AssetID& assetID, string_view reportPath )
    {
        const string_view scenarioPath = assetID.view();
        markWallStart();
        _reportWriter.setReportPath( reportPath );
        string error;
        if ( _scenario.loadFromPath( assetID, error ) == false )
        {
            SW_LOG_ERROR( "[Scenario] could not load '%#': %#", string( scenarioPath ).c_str(), error.c_str() );
            finish( AutomationOutcome::LoadError, error );
            return;
        }
        _outputDirectory = AutomationRunnerInternal::makeOutputDirectory( _scenario.getName() );
        if ( skipIfThisBuildCannotRun() || prepareScratchFolders() == false )
            return;
        SW_LOG_INFO( "[Scenario] %# loaded from %# (%# step(s), fixed delta %#)", _scenario.getName().c_str(), string( scenarioPath ).c_str(),
                     static_cast<uint32>( _scenario.getSteps().size() ), _scenario.getFixedDelta() );
        if ( _scenario.getInputMode() == VirtualInputMode::Mixed )
            SW_LOG_WARNING( "[Scenario] %# uses mixed input - do not touch the keyboard or mouse while it runs", _scenario.getName().c_str() );
        startHangWatchdog();
    }

    void AutomationRunner::requireStartScene( string_view scenePath )
    {
        _startScenePath = string( scenePath );
    }

    bool AutomationRunner::holdForStartScene()
    {
        if ( _startScenePath.empty() )
            return false;
        SceneManager& sceneManager = engine::getSubsystem<SceneManager>();
        if ( _startSceneLoad.isValid() == false )
        {
            const Scene* pActive = sceneManager.getActiveScene();
            if ( pActive == nullptr )
                return true; // 게임이 기동하며 연 씬이 아직 서는 중이다 — 선 뒤에 그 씬인지 본다
            if ( pActive->getSourcePath() == _startScenePath )
            {
                _startScenePath.clear();
                return false;
            }
            _startSceneLoad = sceneManager.requestLoadFuture( _startScenePath, SceneLoadKind::Travel );
            if ( _startSceneLoad.isValid() == false )
            {
                finish( AutomationOutcome::LoadError, "the start scene '" + _startScenePath + "' could not be requested" );
                return true;
            }
            SW_LOG_INFO( "[Scenario] %#: opening the start scene %#", _scenario.getName().c_str(), _startScenePath.c_str() );
            return true;
        }
        if ( _startSceneLoad.isReady() == false )
            return true;
        const Scene* const pLoaded = _startSceneLoad.get();
        if ( pLoaded == nullptr )
        {
            finish( AutomationOutcome::LoadError, "the start scene '" + _startScenePath + "' failed to load (see the log)" );
            return true;
        }
        if ( sceneManager.getActiveScene() != pLoaded )
            return true; // 전환이 끝나기를 기다린다
        _startScenePath.clear();
        return false;
    }

    bool AutomationRunner::startFromText( string_view xmlText, string_view reportPath )
    {
        markWallStart();
        _reportWriter.setReportPath( reportPath );
        string error;
        if ( _scenario.parse( xmlText, "<text>", error ) == false )
        {
            finish( AutomationOutcome::LoadError, error );
            return false;
        }
        _outputDirectory  = AutomationRunnerInternal::makeOutputDirectory( _scenario.getName() );
        const bool bReady = skipIfThisBuildCannotRun() == false && prepareScratchFolders();
        if ( bReady )
            startHangWatchdog();
        return bReady;
    }

    bool AutomationRunner::prepareScratchFolders()
    {
        // 사본 이름에 이 프로세스의 꼬리를 붙인다 — 저장소의 `Content/scratch/` 는 동시에 도는 App 이 나눠 쓴다.
        _scenario.bindScratchInstances();
        _scenario.bindOutputDirectory( _outputDirectory ); // `{out}` — 산출물 폴더도 프로세스마다다(두 시작 길 모두 여기를 지난다)
        for ( const AutomationScratchFolder& folder : _scenario.getTestOptions()._listScratchFolder )
        {
            string error;
            if ( AutomationScratch::prepare( folder, error ) == false )
            {
                SW_LOG_ERROR( "[Scenario] %#: scratch folder %# could not be prepared: %#", _scenario.getName().c_str(), folder._scratchID.c_str(), error.c_str() );
                finish( AutomationOutcome::LoadError, error );
                return false;
            }
            SW_LOG_INFO( "[Scenario] %#: %# copied to %# (fresh for this run)", _scenario.getName().c_str(), folder._sourceID.c_str(), folder._scratchID.c_str() );
        }
        acquireWriteGate(); // 사본을 만든 뒤부터 `scratch/` 밖의 리소스 원본 쓰기를 오류로 만든다
        return true;
    }

    bool AutomationRunner::skipIfThisBuildCannotRun()
    {
        // 키트 · 에디터 확장에 기대는 시나리오는 그 모듈이 없는 빌드(그 키트를 쓰지 않는 활성 게임)에서 건너뛴다 — 단계 검사가 "없는 감시 값" 으로 읽기 오류를 내기 전에 본다.
        const IModuleLoader* const pModuleLoader = engine::findSubsystem<IModuleLoader>(); // 선택: 호스트 없는 하니스(단위 테스트)에는 없다 — 그때는 모듈 선언을 건너뜀으로 본다
        for ( const string& moduleName : _scenario.getTestOptions()._listRequiredModule )
        {
            if ( pModuleLoader != nullptr && pModuleLoader->isModuleActive( moduleName ) )
                continue;
            _bSkippedByBuild = true;
            finish( AutomationOutcome::Skipped, "requiresModule: " + moduleName + " is not loaded in this build" );
            return true;
        }
        const AutomationStep* pMissingStep = AutomationRunnerInternal::findStepMissingFromThisBuild( _scenario );
        if ( pMissingStep == nullptr )
            return false;
        _bSkippedByBuild = true;
        finish( AutomationOutcome::Skipped, pMissingStep->describe() + ": dev commands are compiled out of this build" );
        return true;
    }

    GameObjectManager* AutomationRunner::findActiveObjectManager() const
    {
        if ( _pObjectManagerOverride != nullptr )
            return _pObjectManagerOverride;
        return engine::findActiveObjectManager();
    }

    void AutomationRunner::onFrameBegin( InputManager& input )
    {
        if ( _result != AutomationOutcome::Running )
            return;
        finishIfWallClockExceeded();
        if ( _result != AutomationOutcome::Running )
            return; // 벽시계 상한을 넘었다 — 이 프레임의 `onFrameEnd` 가 끝맺는다
        if ( _bStarted == SW_FALSE )
        {
            // 씬 플레이 중이란 활성 씬이 플레이를 시작했고 시작 차단 조건이 모두 풀렸다는 뜻이다(UI 의 로딩 화면은 게임 입력을 막으므로, 그동안 넣은 입력은 폰에 닿지 않는다).
            // `Immediately` 는 씬이 플레이 중일 필요만 없앤다 — 차단 조건(로딩 화면 · 감시 울타리)은 어느 시작 조건이든 풀려야 시작한다.
            const GameObjectManager* pManager    = findActiveObjectManager();
            const bool               bBlocked    = AutomationStepRegistry::isStartBlocked();
            const bool               bSceneReady = _scenario.getStartCondition() == AutomationStartCondition::Immediately || ( pManager != nullptr && pManager->hasBegunPlay() );
            const bool               bReady      = bSceneReady && bBlocked == false;
            // 배타 시나리오는 시작 조건을 기다리는 프레임에도 사람의 장치를 버린다. 기다리는 동안 게임이 실제 마우스 위치를 읽으면(건설 커서 등)
            // 그 값이 게임 상태에 남아 같은 시나리오가 실행마다 다른 결과를 낸다. 시작할 때 다시 붙여 가상 입력 프레임을 0 부터 센다.
            const bool bBlockWhileWaiting = _scenario.getInputMode() == VirtualInputMode::Exclusive && input.getVirtualInput() != &_inputScript;
            if ( bBlockWhileWaiting )
            {
                input.attachVirtualInput( &_inputScript, VirtualInputMode::Exclusive );
                input.setOSDeviceInputBlocked( true );
            }
            // 전경이 필요한 시나리오는 첫 프레임부터 창을 전경으로 가져오고, 얻을 때까지 시작하지 않는다 — 게임이 시작하며 켜는 상태(1인칭 커서 잠금)가
            // 전경에서 켜지게. 시작 뒤의 단계로는 늦다(게임의 첫 틱이 먼저 돈다). 시작 시한까지 얻지 못하면 이 세션에서 볼 수 없는 시나리오라 건너뛴다.
            const bool bForegroundMissing = _scenario.getTestOptions()._bRequiresForeground && AutomationWindowUtil::bringToForeground() == false;
            // 앞 시나리오(체인)가 남긴 백그라운드 작업이 끝나야 시작한다 — 그 결과 줄이 이 시나리오 창에 떨어지지 않게. 시작 시한은 세지 않는다(벽시계 시한이 본다).
            if ( holdClockForBackgroundWork( input ) )
                return;
            // 시작 씬이 서기 전에는 시작 조건(씬 플레이)을 보지 않는다 — 앞 단계의 씬이 플레이 중이어도 그 씬으로 시작하지 않게.
            if ( holdForStartScene() )
            {
                if ( _result == AutomationOutcome::Running && ++_run._waitFrameCount > _scenario.getStartTimeoutFrames() )
                    finish( AutomationOutcome::TimedOut, "the start scene did not become active within startTimeoutFrames" );
                return;
            }
            if ( bReady == false || bForegroundMissing )
            {
                if ( ++_run._waitFrameCount <= _scenario.getStartTimeoutFrames() )
                    return;
                if ( bReady )
                    finish( AutomationOutcome::Skipped, "the window could not become the foreground window in this session" );
                else if ( bBlocked )
                    finish( AutomationOutcome::TimedOut, "a start blocker (loading screen, content watch fence) never cleared within startTimeoutFrames" );
                else
                    finish( AutomationOutcome::TimedOut, "the start condition never became true" );
                return;
            }
            _pInputManager = &input;
            string error;
            if ( prepare( error ) == false )
            {
                finish( AutomationOutcome::LoadError, error );
                return;
            }
            _bStarted = SW_TRUE;
            input.attachVirtualInput( &_inputScript, _scenario.getInputMode() );
            // 배타 시나리오는 무인 실행이다. 사람이 같은 PC 에서 마우스를 움직여도 에디터 UI 까지 결과가 바뀌지 않게 장치 이벤트를 창 메시지 단계에서 버린다.
            input.setOSDeviceInputBlocked( _scenario.getInputMode() == VirtualInputMode::Exclusive );
            SW_LOG_INFO( "[Scenario] %# started after %# frame(s)", _scenario.getName().c_str(), _run._waitFrameCount );
            updateCrashContext( "started" );
            startLoadWindow();
        }
        else if ( holdClockForBackgroundWork( input ) )
        {
            return; // 이 프레임은 단계를 돌리지 않는다(`onFrameEnd` 도 `_run._bClockHeld` 를 본다)
        }
        _judge.setFrameIndex( _run._frameIndex );
        // 액션 계층 단계(레지스트리의 `_bBeforeInput`)는 그 프레임의 입력 재생과 게임 틱 전에 넣는다.
        const vector<AutomationStep>& listStep = _scenario.getSteps();
        for ( size_t stepIndex = _run._nextStepIndex; stepIndex < listStep.size() && listStep[stepIndex]._frameIndex == _run._frameIndex; ++stepIndex )
        {
            const AutomationStepRegistration* pRegistration    = AutomationStepRegistry::find( listStep[stepIndex]._kind );
            const bool                        bRunsBeforeInput = pRegistration != nullptr && pRegistration->_bBeforeInput && pRegistration->_pRun != nullptr;
            if ( bRunsBeforeInput && pRegistration->_pRun( *this, listStep[stepIndex] ) == false )
                finish( AutomationOutcome::LoadError, listStep[stepIndex].describe() + ": the step could not run" );
        }
    }

    AutomationOutcome AutomationRunner::onFrameEnd( InputManager& input )
    {
        // 붙잡힌 프레임도 센다 — 시계를 붙잡는 동안에도 크래시 자리와 벽시계 감시가 갱신돼야 한다.
        ++_run._seenFrameCount;
        _frameStats.onFrameEnd(); // 벤치마크 구간이면 이 프레임의 벽시계 시간을 쌓는다
        const size_t stepIndexBefore = _run._nextStepIndex;
        finishIfWallClockExceeded();
        const bool bRefreshCrashContext = _bStarted == SW_TRUE && _result == AutomationOutcome::Running && _run._seenFrameCount % AutomationRunTitleUtil::kTitleRefreshFrames == 0;
        if ( bRefreshCrashContext )
            updateCrashContext();
        const bool bRunsSteps = _result == AutomationOutcome::Running && _bStarted == SW_TRUE && _run._bClockHeld == SW_FALSE; // 시계 잡기 조건이 붙잡은 프레임은 단계를 돌리지 않는다
        if ( bRunsSteps )
        {
            _judge.sampleWatches( *this );
            const vector<AutomationStep>& listStep = _scenario.getSteps();
            // `<=` 이므로 스크린샷을 기다리느라 밀린 단계는 다음 프레임에 이어서 돈다.
            while ( true )
            {
                const bool bNextStepDue = _run._nextStepIndex < listStep.size() && listStep[_run._nextStepIndex]._frameIndex <= _run._frameIndex && _result == AutomationOutcome::Running;
                if ( bNextStepDue == false )
                    break;
                const AutomationStep&             step          = listStep[_run._nextStepIndex];
                const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
                const AutomationStepHold          hold          = pRegistration != nullptr && pRegistration->_pHold != nullptr ? pRegistration->_pHold( *this, step, input )
                                                                                                                               : AutomationStepHold::Run;
                if ( hold == AutomationStepHold::Hold )
                    break;
                _run._imageWaitFrameCount = 0;
                ++_run._nextStepIndex;
                _wallProfile.closeWait( step._kind, step._orderInFile, _run._frameIndex ); // 앞서 쌓인 고정 대기 · 조건 대기를 이 단계 앞의 대기로 닫는다
                if ( hold == AutomationStepHold::Skip )
                    continue;
                // 등록이 없으면 검사 뒤에 그 단계를 등록한 모듈이 내려갔다(핫 리로드). 실행 함수가 없는 단계(입력)는 준비에서 이미 끝났다.
                const bool   bRunsNow        = pRegistration != nullptr && pRegistration->_bBeforeInput == false && pRegistration->_pRun != nullptr;
                const uint32 requestedBefore = _run._screenshotRequestedCount;
                // 단계 전환 한 줄: 실행 함수가 있는 단계만 남긴다(입력 단계는 가상 입력으로 한꺼번에 넘어가 프레임마다 수십 줄이 되므로 뺀다).
                const bool bHasRunFunction = pRegistration != nullptr && pRegistration->_pRun != nullptr;
                if ( bHasRunFunction )
                {
                    _run._currentStepKind   = step._kind;
                    _run._currentStepNumber = step._orderInFile;
                    const string stepLine   = AutomationRunTitleUtil::makeStepLogLine( _scenario.getName(), _run._currentStepNumber, _run._currentStepKind, _run._frameIndex );
                    SW_LOG_INFO( "%#", stepLine.c_str() );
                    CrashHandler::addBreadcrumb( stepLine ); // 크래시 보고에 최근 단계 전환이 남는다
                    updateCrashContext();
                }
                if ( pRegistration == nullptr || ( bRunsNow && pRegistration->_pRun( *this, step ) == false ) )
                    finish( AutomationOutcome::LoadError, step.describe() + ": the step could not run" );
                // 사진은 다음 렌더 패킷이 찍는다 — 같은 프레임에 뒤 단계가 돌면 그 변화가 사진에 들어간다. 사진을 요청한 프레임은 여기서 닫고 뒤 단계는 다음 프레임에 돈다.
                if ( _run._screenshotRequestedCount != requestedBefore )
                    break;
            }
            const bool bExitDeadlineExceeded = _result == AutomationOutcome::Running && _run._exitDeadlineSeconds > 0.0 &&
                                               _run._exitDeadlineSeconds < static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
            if ( bExitDeadlineExceeded )
                finish( AutomationOutcome::Failed, "the window was asked to close but the loop kept running past ExpectExitWithin" );
            // 단계 시점(`_frameIndex`)은 `WaitUntil` 이 붙잡은 프레임에 서 있다(뒤 단계가 그만큼 밀린다). 시한은 붙잡힌 프레임까지 모두 센 `_elapsedFrameCount` 로 본다 —
            // 예전에는 붙잡힌 프레임을 세지 않아 조건이 끝내 서지 않는 시나리오를 프레임 시한이 잡지 못하고 벽시계 그물만 잡았다.
            // 시나리오 끝(`<Pass/>`)이 뒤의 일(캡처 파일 쓰기)을 기다리는 프레임은 세지 않는다 - 단계는 이미 다 돌았고 그 대기는 따로 묶인다.
            if ( _result == AutomationOutcome::Running && _run._bFinishWaiting == false )
            {
                ++_run._elapsedFrameCount;
                if ( _run._holdFrameCount == 0 )
                    ++_run._frameIndex;
            }
            // 창 닫기를 기다리는 동안은 프레임 시한을 보지 않는다(벽시계 시한이 본다). 부하 시나리오는 측정 창이 끝내므로 벽시계 그물만 본다.
            const bool bFrameTimeoutReached = _result == AutomationOutcome::Running && _run._exitDeadlineSeconds <= 0.0 && _run._loadDeadlineSeconds <= 0.0 &&
                                              _run._elapsedFrameCount > _scenario.getTimeoutFrames();
            if ( bFrameTimeoutReached )
                finish( AutomationOutcome::TimedOut, "timeoutFrames reached without <Pass/>" );
            const bool bLoadDeadlineExceeded = _result == AutomationOutcome::Running && _run._loadDeadlineSeconds > 0.0 &&
                                               _run._loadDeadlineSeconds < static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
            if ( bLoadDeadlineExceeded )
                finish( AutomationOutcome::TimedOut, "the <Load> measurement window did not close in time (was -gv_profileFrames also given?)" );
        }
        if ( _bEnded == SW_FALSE )
            recordTimelineFrame( stepIndexBefore );
        if ( _result != AutomationOutcome::Running )
            endRun( &input );
        return _result;
    }

    void AutomationRunner::startLoadWindow()
    {
        const AutomationLoad& load = _scenario.getTestOptions()._load;
        if ( load._bDeclared == false )
            return;
        // 측정 창은 시나리오가 시작한 프레임부터다(로딩을 빼고 측정한다). 끝은 측정 보고가 낸 종료 요청이다(`FrameProfileSession` 이 늦게 건 목표를 읽는다).
        const int32 seconds = static_cast<int32>( MathUtil::ceil( load._durationSeconds ) );
        if ( engine::getSubsystem<GlobalVariableManager>().setValueFromString( "gv_profileSeconds", to_string( seconds ) ) == false )
        {
            finish( AutomationOutcome::LoadError, "<Load> could not set gv_profileSeconds (a build without the frame profiler?)" );
            return;
        }
        // 멈춤 그물 — 워밍업(60 프레임)과 느린 기계를 넉넉히 덮는다.
        _run._loadDeadlineSeconds = AutomationRunnerInternal::readNowSeconds() + 2.0 * static_cast<float64>( seconds ) + AutomationTimeLimits::kLoadDeadlineSlackSeconds;
        SW_LOG_INFO( "[Scenario] load window %# s (clients %#, meshes %#, movers %#, network '%#')", seconds, load._clientCount, load._meshCount, load._moverCount,
                     load._network.c_str() );
    }

    void AutomationRunner::markWallStart()
    {
        _run._wallStartSeconds    = AutomationRunnerInternal::readNowSeconds();
        _run._lastFrameEndSeconds = _run._wallStartSeconds;
        _startedUnixMs            = WallClock::nowUnixMilliseconds().count();
        LocalCalendarTime calendar{};
        if ( WallClock::toLocalCalendar( _startedUnixMs / 1000, calendar ) == false )
            return; // 달력으로 못 바꾸는 플랫폼이면 지역 시각 글은 비운다(유닉스 밀리초는 남는다)
        StringBuilder<constant::kMaxBuffer64> text;
        text.appendFormat( "%04d-%02d-%02d %02d:%02d:%02d", calendar._year, calendar._month, calendar._day, calendar._hour, calendar._minute, calendar._second );
        _startedAt = text.c_str();
    }

    void AutomationRunner::recordTimelineFrame( size_t stepIndexBefore )
    {
        AutomationFrameKind kind = AutomationFrameKind::Start;
        if ( _bStarted == SW_TRUE )
        {
            const bool bStepRan = _run._nextStepIndex != stepIndexBefore;
            if ( _run._bClockHeld == SW_TRUE )
                kind = AutomationFrameKind::BackgroundHold;
            else if ( _run._holdFrameCount > 0 )
                kind = AutomationFrameKind::ConditionHold;
            else
                kind = bStepRan ? AutomationFrameKind::StepRan : AutomationFrameKind::ScheduledWait;
        }
        const float64 nowSeconds = AutomationRunnerInternal::readNowSeconds();
        _wallProfile.recordFrame( kind, nowSeconds - _run._lastFrameEndSeconds );
        if ( kind == AutomationFrameKind::StepRan )
            _wallProfile.recordStepFrame( _run._currentStepKind, nowSeconds - _run._lastFrameEndSeconds );
        _run._lastFrameEndSeconds = nowSeconds;
    }

    void AutomationRunner::finishIfWallClockExceeded()
    {
        // 부하 시나리오는 측정 창을 닫는 그물(`_run._loadDeadlineSeconds`)이 따로 있어 길이가 `durationSeconds` 에 매인다.
        const bool bWatches = _result == AutomationOutcome::Running && _scenario.getTestOptions()._load._bDeclared == false;
        if ( bWatches == false )
            return;
        const float64 elapsedSeconds = AutomationRunnerInternal::readNowSeconds() - _run._wallStartSeconds;
        if ( elapsedSeconds <= static_cast<float64>( _scenario.getWallTimeoutSeconds() ) )
            return;
        string reason = formatToString( "the wall-clock limit of %# s (wallTimeoutSeconds) was exceeded at %#", _scenario.getWallTimeoutSeconds(), describeRunPosition() );
        if ( _bStarted == SW_FALSE )
            reason += formatToString( ", the scenario never started (%# frame(s) spent waiting for the start condition)", _run._waitFrameCount );
        if ( _run._holdFrameCount > 0 )
            reason += formatToString( ", the current step has held the scenario clock for %# frame(s)", _run._holdFrameCount );
        if ( _run._clockHoldFrameCount > 0 )
            reason += formatToString( ", %# has held the scenario clock for %# frame(s)", _run._clockHolderName, _run._clockHoldFrameCount );
        finish( AutomationOutcome::TimedOut, reason );
    }

    string AutomationRunner::findBackendName()
    {
        // 창이 없는 호스트는 디바이스를 만들지 않아 `gv_rhiBackend` 가 설정 기본값일 뿐이다 — 그때는 적지 않는다.
        if ( engine::areEngineServicesBound() == false )
            return string{};
        const MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        if ( pWindowHost == nullptr || pWindowHost->getWindow() == nullptr )
            return string{};
        return string{ RHI::getBackendTypeName( gv_rhiBackend ) };
    }

    string AutomationRunner::describeRunPosition() const
    {
        const string backendName = findBackendName();
        // 이름이 없으면(읽기 오류) 요약이 벤치 실행으로 읽힌다 — 자리표시 이름을 쓴다.
        const string            scenarioName = _scenario.getName().empty() ? string( "(scenario not loaded)" ) : _scenario.getName();
        AutomationRunTitleInput input;
        input._scenarioName = scenarioName;
        input._backendName  = backendName;
        input._stepKind     = _run._currentStepKind;
        input._stepNumber   = _run._currentStepNumber;
        input._frame        = _run._frameIndex;
        return AutomationRunTitleUtil::makeSummary( input );
    }

    void AutomationRunner::updateCrashContext( string_view note )
    {
        const string position = describeRunPosition();
        string       text     = position;
        if ( note.empty() == false )
        {
            text += " (";
            text += note;
            text += ')';
        }
        // 키 하나가 시나리오 · 백엔드 · 단계 · 프레임을 든다. 값 길이를 넘으면 글자 경계에서 자른다(저장소가 조용히 자르면 UTF-8 중간이 깨진다).
        CrashHandler::setContextValue( "Scenario", AutomationRunTitleUtil::truncateUTF8( text, CrashContextStore::kMaxValueLength - 1 ) );
        publishHangSnapshot( position ); // 행 감시 스레드가 읽을 같은 자리(단계 전환 때와 30 프레임마다)
    }

    string AutomationRunner::makeReproduceCommand() const
    {
        AutomationReproduceInput reproduceInput = _reproduceInput;
        for ( const AutomationAttribute& variable : _scenario.getTestOptions()._listLaunchVariable )
        {
            reproduceInput._listExtraArgument.push_back( "-" + variable._name + "=" + variable._value ); // `<Launch>` 변수는 명령줄에 있어야 시작한다
        }
        return AutomationRunTitleUtil::makeReproduceCommand( reproduceInput );
    }

    void AutomationRunner::startHangWatchdog()
    {
        const bool bWatches = _bHangWatchArmed == SW_TRUE && _result == AutomationOutcome::Running && _scenario.getTestOptions()._load._bDeclared == false;
        if ( bWatches == false )
            return;
        // 바뀌지 않는 칸은 지금 한 번 채운다. 행에서는 이것만 읽는다(감시 스레드는 실행기 멤버를 건드리지 않는다).
        _hangPayload = AutomationHangPayload{};
        AutomationHangPayload::assign( _hangPayload._scenario, AutomationHangPayload::kScenarioBytes, _scenario.getName() );
        AutomationHangPayload::assign( _hangPayload._game, AutomationHangPayload::kGameBytes, _reproduceInput._game );
        AutomationHangPayload::assign( _hangPayload._startedAt, AutomationHangPayload::kStartedAtBytes, _startedAt );
        AutomationHangPayload::assign( _hangPayload._reproduce, AutomationHangPayload::kReproduceBytes, makeReproduceCommand() );
        AutomationHangPayload::assign( _hangPayload._reportPath, AutomationHangPayload::kReportPathBytes, _reportWriter.getReportPath() );
        AutomationHangPayload::assign( _hangPayload._result, AutomationHangPayload::kResultBytes, getEnumName( AutomationOutcome::TimedOut ) );
        _hangPayload._startedUnixMs         = _startedUnixMs;
        _hangPayload._wallLimitMilliseconds = static_cast<uint32>( static_cast<float64>( _scenario.getWallTimeoutSeconds() ) * 1000.0 );
        // 행에서는 보고 폴더를 만들 수 없다(할당 · 잠금 없이 쓴다) — 미리 만들어 둔다.
        const string reportDirectory = FileUtil::getDirectoryPart( _reportWriter.getReportPath() );
        if ( reportDirectory.empty() == false )
            (void)FileUtil::ensureDirectoryExists( reportDirectory ); // 못 만들면 행 보고만 못 쓴다 — 정상 종료의 보고는 자기 쓰기가 알린다
        _hangWatchdog.publish( _hangPayload );                        // 감시가 뜨기 전에 한 번 채운다
        AutomationHangWatchdog::Settings settings;
        settings._startSeconds = _run._wallStartSeconds;
        settings._limitSeconds = _scenario.getWallTimeoutSeconds();
        settings._graceSeconds = AutomationTimeLimits::kHangGraceSeconds;
        settings._exitCode     = getAutomationExitCode( AutomationOutcome::TimedOut );
        _hangWatchdog.start( settings );
    }

    void AutomationRunner::publishHangSnapshot( string_view position )
    {
        if ( _hangWatchdog.isRunning() == false )
            return;
        AutomationHangPayload::assign( _hangPayload._lastStep, AutomationHangPayload::kLastStepBytes, position );
        AutomationHangPayload::assign( _hangPayload._backend, AutomationHangPayload::kBackendBytes, findBackendName() );
        _hangPayload._frame = _run._frameIndex;
        _hangWatchdog.publish( _hangPayload );
    }

    AutomationOutcome AutomationRunner::onWindowClosed( InputManager* pInput )
    {
        if ( _result == AutomationOutcome::Running )
        {
            if ( _run._exitDeadlineSeconds > 0.0 )
            {
                const float64 nowSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
                if ( nowSeconds <= _run._exitDeadlineSeconds )
                    finish( AutomationOutcome::Passed, "the window closed within ExpectExitWithin" );
                else
                    finish( AutomationOutcome::Failed, "the window closed after the ExpectExitWithin deadline" );
            }
            else if ( _run._loadDeadlineSeconds > 0.0 && _run._nextStepIndex >= _scenario.getSteps().size() )
            {
                // 부하 시나리오의 정상 끝 — 측정 창이 닫혀 엔진이 종료를 요청했다(보고가 남았는지는 실행기가 출력으로 본다).
                finish( AutomationOutcome::Passed, "the <Load> measurement window closed" );
            }
            else
            {
                finish( AutomationOutcome::Failed, "the window closed before the scenario ended" );
            }
        }
        endRun( pInput );
        return _result;
    }

    void AutomationRunner::stop( InputManager* pInput, string_view reason )
    {
        if ( _result != AutomationOutcome::Running )
            return;
        finish( AutomationOutcome::Skipped, reason );
        endRun( pInput );
    }

    void AutomationRunner::recordFailure( const AutomationStep& step, string_view message )
    {
        _judge.addFailure( step, message );
    }

    void AutomationRunner::endScenario( AutomationStepEnd end, string_view reason )
    {
        // 단계의 말(Pass · Fail · Skip)을 종료 코드로 번역하는 곳은 여기 하나다. 새 끝 까닭이 늘면 스위치가 컴파일러 경고로 알린다.
        switch ( end )
        {
            case AutomationStepEnd::Pass:
            {
                finish( AutomationOutcome::Passed, reason );
                return;
            }
            case AutomationStepEnd::Fail:
            {
                finish( AutomationOutcome::Failed, reason );
                return;
            }
            case AutomationStepEnd::Skip:
            {
                finish( AutomationOutcome::Skipped, reason );
                return;
            }
        }
    }

    void AutomationRunner::finish( AutomationOutcome result, string_view reason )
    {
        if ( _result != AutomationOutcome::Running )
            return;
        _result       = ( result == AutomationOutcome::Passed && _judge.getFailures().empty() == false ) ? AutomationOutcome::Failed : result;
        _finishReason = string( reason );
    }

    bool AutomationRunner::prepare( string& outError )
    {
        // 기동 때 정해지는 값(첫 씬 · 렌더 파이프라인)은 시나리오가 바꿀 수 없다 — 띄운 명령줄이 선언과 다르면 다른 화면을 단언하게 되므로 멈춘다.
        for ( const AutomationAttribute& variable : _scenario.getTestOptions()._listLaunchVariable )
        {
            const GlobalVariableInfo* pInfo           = engine::areEngineServicesBound() ? engine::getSubsystem<GlobalVariableManager>().findVariable( variable._name ) : nullptr;
            bool                      bMatched        = false;
            const bool                bLaunchedWithIt = pInfo != nullptr && AutomationStepArgsUtil::compareVariable( *pInfo, string_view{ variable._value }, 0.0f, bMatched ) && bMatched;
            if ( bLaunchedWithIt == false )
            {
                outError = "<Launch> needs the command line -" + variable._name + "=" + variable._value + " (now " +
                           ( pInfo != nullptr ? "'" + pInfo->getValueAsString() + "'" : string( "no such variable" ) ) + ")";
                return false;
            }
        }
        if ( AutomationStepValidation::validateDesktopSteps( _scenario, outError ) == false )
            return false;
        for ( const AutomationStep& step : _scenario.getSteps() )
        {
            const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
            if ( pRegistration == nullptr )
            {
                outError = step.describe() + ": unknown step (nothing registered under that name)";
                return false;
            }
            if ( pRegistration->_pValidate != nullptr && pRegistration->_pValidate( step, outError ) == false )
                return false;
            if ( pRegistration->_pPrepare != nullptr && pRegistration->_pPrepare( *this, step, outError ) == false )
                return false;
        }
        return _judge.prepare( _scenario, outError ); // 허용 목록이 틀리면 폴백 없이 읽기 오류(11)로 멈춘다
    }

    AutomationStepHold AutomationRunner::holdUntilReady( [[maybe_unused]] const AutomationStep& step, InputManager& input, bool bReady, uint32 timeoutFrames )
    {
        if ( bReady || _run._holdFrameCount >= timeoutFrames )
        {
            if ( bReady && _run._holdFrameCount > 0 )
                SW_LOG_INFO( "[Scenario] %# held the clock for %# frame(s)", step.describe().c_str(), _run._holdFrameCount );
            _run._holdFrameCount = 0;
            _run.clearHeldStepState(); // 놓인 단계의 상태는 이번 붙잡기의 것이다
            return AutomationStepHold::Run;
        }
        ++_run._holdFrameCount;
        // 이 프레임은 시나리오 프레임으로 세지 않는다. 다음 프레임에 낼 입력 이벤트부터 한 프레임씩 밀어 시나리오 프레임과 맞춘다.
        _inputScript.delayEventsFrom( input.getVirtualFrameIndex(), 1 );
        return AutomationStepHold::Hold;
    }

    IAutomationStepState* AutomationRunner::findHeldStepState( const AutomationStep& step ) const
    {
        return _run._pHeldStateStep == &step ? _run._pHeldStepState.get() : nullptr;
    }

    IAutomationStepState& AutomationRunner::setHeldStepState( const AutomationStep& step, unique_ptr<IAutomationStepState> state )
    {
        SW_ASSERT( state != nullptr );
        _run._pHeldStepState = std::move( state );
        _run._pHeldStateStep = &step;
        return *_run._pHeldStepState;
    }

    bool AutomationRunner::holdClockForBackgroundWork( InputManager& input )
    {
        const utf8* pHolder = AutomationStepRegistry::findClockHolder();
        if ( pHolder == nullptr )
        {
            if ( _run._clockHoldFrameCount > 0 )
                SW_LOG_INFO( "[Scenario] %# held the clock for %# frame(s)", _run._clockHolderName.c_str(), _run._clockHoldFrameCount );
            _run._clockHoldFrameCount = 0;
            _run._bClockHeld          = SW_FALSE;
            return false;
        }
        const float64 nowSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
        if ( _run._clockHoldFrameCount == 0 )
        {
            _run._clockHoldStartSeconds = nowSeconds;
            _run._clockHolderName       = pHolder;
        }
        ++_run._clockHoldFrameCount;
        _run._bClockHeld = SW_TRUE;
        // 시작한 시나리오는 이 프레임을 세지 않는다. 이 프레임에 낼 입력 이벤트부터 한 프레임씩 밀어 시나리오 프레임과 맞춘다(`holdUntilReady` 와 같다).
        if ( _bStarted == SW_TRUE )
            _inputScript.delayEventsFrom( input.getVirtualFrameIndex(), 1 );
        // 시한은 `AutomationTimeLimits` 한 곳에 있다(저장 검증은 쉬는 기계에서 몇 초, 빌드가 겹치면 30 초를 넘긴다).
        if ( nowSeconds - _run._clockHoldStartSeconds > AutomationTimeLimits::kClockHoldTimeoutSeconds )
            finish( AutomationOutcome::TimedOut, _run._clockHolderName + " held the scenario clock for more than " +
                                                     formatToString( "%# s", static_cast<uint32>( AutomationTimeLimits::kClockHoldTimeoutSeconds ) ) );
        return true;
    }

    bool AutomationRunner::shouldHoldForCondition( const AutomationStep& step, InputManager& input )
    {
        bool                                    bMatched = false;
        string                                  message;
        const bool                              bHasValue = _judge.compareWatch( *this, step, bMatched, message );
        const bool                              bReady    = bHasValue && bMatched;
        string                                  error;
        AutomationStepArgsUtil::WaitTimeoutArgs timeout;
        if ( AutomationStepArgsUtil::parseWaitTimeout( step, timeout, error ) == false )
        {
            recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드) — 기다리지 않고 실패로 적는다
            return false;
        }
        const uint32 timeoutFrames  = timeout._bSeconds ? convertSecondsToFrames( timeout._timeoutSeconds ) : timeout._timeoutFrames;
        const uint32 heldFrameCount = _run._holdFrameCount;
        if ( holdUntilReady( step, input, bReady, timeoutFrames ) == AutomationStepHold::Hold )
            return true;
        if ( bReady == false )
            recordFailure( step, formatToString( "WaitUntil gave up after %# frame(s): %#", heldFrameCount, message ) );
        return false;
    }

    uint32 AutomationRunner::convertSecondsToFrames( float32 seconds ) const
    {
        return static_cast<uint32>( seconds / _scenario.getFixedDelta() + 0.5f );
    }

    string AutomationRunner::resolveOutputPath( string_view file ) const
    {
        const bool bAbsolute = file.empty() == false && ( file[0] == '/' || file[0] == '\\' || ( file.size() > 1 && file[1] == ':' ) );
        return bAbsolute ? string( file ) : _outputDirectory + "/" + string( file );
    }

    void AutomationRunner::endRun( InputManager* pInput )
    {
        if ( _bEnded == SW_TRUE )
            return;
        _bEnded              = SW_TRUE;
        _run._wallEndSeconds = AutomationRunnerInternal::readNowSeconds();
        _run.clearHeldStepState(); // 붙잡힌 채 끝난 단계의 상태(요청한 씬 로드 …)를 이 실행과 함께 걷는다
        releaseWriteGate();
        _hangWatchdog.stop(); // 정상 끝맺음이 보고를 쓴다 — 감시가 같은 보고를 덮거나 프로세스를 끝내지 못하게 먼저 내린다
        if ( pInput != nullptr && pInput->getVirtualInput() == &_inputScript )
            pInput->detachVirtualInput();
        _judge.stopLogCapture();
        // 시작한 시나리오는 결과와 상관없이 로그 게이트를 지난다 — 시작하지 못한 실행은 허용 목록도 로그 수집도 준비되지 않았다.
        if ( _bStarted == SW_TRUE )
            _judge.applyLogGate( _result, _finishReason );
        if ( _result == AutomationOutcome::Passed )
        {
            SW_LOG_INFO( "[Scenario] PASS %# (%# frame(s))", _scenario.getName().c_str(), _run._frameIndex );
        }
        else if ( _result == AutomationOutcome::Skipped && _bSkippedByBuild )
        {
            // 시나리오가 선언한 조건(이 빌드에 없는 모듈 · 빠진 개발 명령)이다 - 결함이 아니라 빌드 구성이다(언리얼 자동화의 조건부 제외와 같다).
            SW_LOG_INFO( "[Scenario] SKIP %#: %#", _scenario.getName().c_str(), _finishReason.c_str() );
        }
        else if ( _result == AutomationOutcome::Skipped )
        {
            SW_LOG_WARNING( "[Scenario] SKIP %#: %#", _scenario.getName().c_str(), _finishReason.c_str() );
        }
        else
        {
            string detail = _finishReason;
            for ( const string& failure : _judge.getFailures() )
            {
                detail += "\n    ";
                detail += failure;
            }
            // 진 실행의 처리하지 않은 줄은 `failures` 에 섞지 않고 보고의 `unhandledLogs` 에 둔다 — 로그에는 있다는 사실만 적는다.
            if ( _result != AutomationOutcome::UnhandledLog && _judge.getUnhandledLogs().empty() == false )
                detail += formatToString( "\n    (%# unhandled Warning/Error log line(s) also remain - see unhandledLogs in the report)", static_cast<uint32>( _judge.getUnhandledLogs().size() ) );
            SW_LOG_ERROR( "[Scenario] %# %# at frame %#: %#", getEnumName( _result ), _scenario.getName().c_str(), _run._frameIndex, detail.c_str() );
        }
        // 결과마다 말씨가 다른 위 줄과 별개로, 시작 줄(`started after`)과 짝이 되는 같은 모양의 끝 줄을 남긴다(grep 한 번으로 시작 · 끝을 묶는다).
        SW_LOG_INFO( "[Scenario] %# finished %# frames %#", _scenario.getName().c_str(), getEnumName( _result ), _run._frameIndex );
        SW_LOG_INFO( "[Scenario] %# timeline: %#", _scenario.getName().c_str(), _wallProfile.describe().c_str() );
        AutomationReportParams report{};
        report._scenarioName       = _scenario.getName();
        report._pResultName        = getEnumName( _result );
        report._exitCode           = getAutomationExitCode( _result );
        report._frameCount         = _run._frameIndex;
        report._reason             = _finishReason;
        report._pListFailure       = &_judge.getFailures();
        report._pListUpdatedGolden = &_judge.getUpdatedGoldens();
        report._pListUnhandledLog  = &_judge.getUnhandledLogs();
        report._pListAllowedLog    = &_judge.getAllowedLogIDs();
        report._pListMetric        = &_judge.getMetricLines();
        report._pListRecord        = &_judge.getRecordLines();
        // 실행 환경 — 이 보고만으로 어디서 어떻게 돌았고 어떻게 다시 돌리는지 알게 한다.
        const string  backendName        = findBackendName();
        const string  lastStep           = describeRunPosition();
        const string  reproduce          = makeReproduceCommand();
        const bool    bWatchedByWallTime = _scenario.getTestOptions()._load._bDeclared == false;
        const float64 wallSeconds        = _run._wallEndSeconds > _run._wallStartSeconds ? _run._wallEndSeconds - _run._wallStartSeconds : 0.0;
        report._backendName              = backendName;
        report._gameName                 = _reproduceInput._game;
        report._startedAt                = _startedAt;
        report._startedUnixMs            = _startedUnixMs;
        report._wallMilliseconds         = static_cast<uint32>( wallSeconds * 1000.0 );
        report._wallLimitMilliseconds    = bWatchedByWallTime ? static_cast<uint32>( static_cast<float64>( _scenario.getWallTimeoutSeconds() ) * 1000.0 ) : 0u;
        report._lastStep                 = lastStep;
        report._reproduce                = reproduce;
        report._pTimeline                = &_wallProfile;
        _reportWriter.write( report );
        updateCrashContext( string( "ended " ) + getEnumName( _result ) );
    }
} // namespace sw
