#include "pch.h"

#include "DevTools/Client/Capture/StartupCaptureSubsystem.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct StartupCaptureInternal
        {
            /** @brief 첫 장까지 최소로 기다리는 프레임 수입니다. 첫 프레임에는 GPUScene 업로드가 아직이라 그릴 것이 없다. */
            static constexpr uint32 kMinWarmupFrames = 10;
            /** @brief 자리 잡음을 기다리는 벽시계 시한(초)입니다. Debug 빌드가 큰 씬을 읽고 셰이더를 처음 만드는 시간보다 넉넉하다. */
            static constexpr uint32 kStabilityTimeoutSeconds = 120;
            /** @brief 같은 시한(마이크로초)입니다 — 단조 시계와 견준다. */
            static constexpr int64 kStabilityTimeoutMicroseconds = static_cast<int64>( kStabilityTimeoutSeconds ) * constant::time::kMicrosecondsPerSecond;

            /** @brief 렌더 타깃 목록에서 @p name 인 첨부를 찾습니다. 없으면 false 입니다. */
            static bool findAttachment( string_view name, RenderTargetInfo& outInfo )
            {
                const RenderTargetRegistry* const pRegistry = engine::findSubsystem<RenderTargetRegistry>();
                if ( pRegistry == nullptr )
                    return false;
                vector<RenderTargetInfo> listTarget;
                pRegistry->snapshot( listTarget );
                for ( RenderTargetInfo& info : listTarget )
                {
                    if ( string_view{ info._name } == name )
                    {
                        outInfo = std::move( info );
                        return true;
                    }
                }
                return false;
            }
        };
    } // namespace

    /**
     * @brief `-gv_screenshot=<파일경로>` 입니다. 화면에 나간 이미지(Present 결과)를 한 장 씁니다(확장자가 `.png` 면 PNG, 아니면 PPM — 백엔드별 시각 검증용).
     * @details Win32 PrintWindow 캡처는 DX11, GL, Vulkan 에서 빈 화면이 자주 나온다(스왑체인이 GDI 로 합성되지 않는다). 그래서 렌더 스레드가 GPU 에서 직접 읽는다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_screenshot, "", "화면에 나간 그림(Present 결과)을 덤프할 경로 (비면 사용 안 함)" );

    /** @brief `-gv_screenshotAttachment=<이름>` 입니다. Present 결과 대신 이 렌더 타깃(파이프라인 XML 의 첨부 이름, `RenderTargetRegistry`)을 씁니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_screenshotAttachment, "", "Present 결과 대신 덤프할 렌더 타깃(첨부) 이름 (비면 Present 결과)" );

    /**
     * @brief `-gv_screenshotFrame=<N>` 입니다. 몇 번째 프레임에서 찍을지 정합니다(기본 10이고, 10보다 작으면 10으로 봅니다).
     * @details 시간에 따라 움직이는 것(GPU 인스턴스 회전 등)을 검증하려면 **서로 다른 시각**의 장면이 필요하다. `-gv_profileFrames` 는 찍는 시각을 바꾸지 않는다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_screenshotFrame, static_cast<int32>( StartupCaptureInternal::kMinWarmupFrames ), "스크린샷을 찍을 프레임 번호 (기본 10)" );

    /**
     * @brief `-gv_screenshotSettledFrames=<N>` 입니다. 1 이상이면 `-gv_screenshotFrame` 대신, 화면이 자리 잡은 프레임부터 N 프레임을 이어 센 뒤 찍습니다.
     * @details 자리 잡음은 활성 씬이 플레이를 시작했고 로딩 화면 · 화면 페이드 · 에셋 읽기가 끝난 것이다. 로딩 화면의 최소 표시 시간과 페이드는 실제 시간이라,
     *          프레임이 빠른 기계에서는 고정 프레임 번호가 로딩 화면을 찍는다(골든 이미지). 다 세기 전에 자리 잡음이 풀리면 처음부터 다시 센다.
     *          시한(`kStabilityTimeoutSeconds`) 안에 다 세지 못하면 오류로 적고 찍지 않는다(`-gv_screenshotQuit` 은 `CaptureWriteFailed` 로 끝난다).
     *          언리얼 자동화 스크린샷이 로드 · 스트리밍이 끝나기를 기다린 뒤 지연을 두고 찍는 것과 같다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_screenshotSettledFrames, 0, "1 이상이면 화면이 자리 잡은(로딩 화면 · 페이드 · 에셋 읽기 끝) 뒤 이 프레임 수만큼 지나 찍는다 (0=gv_screenshotFrame)" );

    /**
     * @brief `-gv_screenshotCount=<N>` 과 `-gv_screenshotInterval=<K>` 입니다. 첫 장부터 K 프레임마다 N 장을 찍습니다(움직임과 튐 확인용).
     * @details 두 장 이상이면 파일 이름의 확장자 앞에 `_000`, `_001` 등이 붙는다. 장은 한 번에 하나씩 맡긴다(앞 장을 다 쓴 뒤 다음 장) — 쓰기가 K 프레임보다 길면 간격이 그만큼 늘어난다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_screenshotCount, 1, "연속으로 찍을 스크린샷 수 (기본 1)" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_screenshotInterval, 1, "연속 스크린샷 사이 프레임 수 (기본 1)" );

    /**
     * @brief `-gv_screenshotQuit` 입니다. `-gv_screenshot` · `-gv_screenshotEditor` 로 요청한 사진을 모두 찍으면 앱이 스스로 끝납니다(`Passed`, 쓰지 못했으면 `CaptureWriteFailed`).
     * @details 기본은 끄기입니다. `-gv_profileFrames` 와 함께 쓰는 실행이 사진 뒤에 일찍 끝나지 않게 하려는 것입니다. 사진 인자를 주지 않으면 끝낼 근거가 없어 아무 일도 하지 않습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_screenshotQuit, false, "요청한 스크린샷을 모두 찍으면 앱이 스스로 끝난다 (Passed, 파일을 쓰지 못했으면 CaptureWriteFailed)" );

    StartupCaptureSubsystem::StartupCaptureSubsystem()
        : _expectedMask{ 0u }
        , _finishedMask{ 0u }
        , _failedMask{ 0u }
        , _pendingPath{}
        , _pendingSerial{ 0 }
        , _frameCounter{ 0 }
        , _shotCount{ 0 }
        , _waitStartMicroseconds{ 0 }
        , _bGameViewStarted{ SW_FALSE }
        , _bGameViewFinished{ SW_FALSE }
        , _bGameViewFailed{ SW_FALSE }
    {
    }

    StartupCaptureSubsystem::~StartupCaptureSubsystem() = default;

    span<const utf8* const> StartupCaptureSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { RenderThreadSubsystem::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask StartupCaptureSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }

    void StartupCaptureSubsystem::reportCaptureFinished( StartupCaptureKind kind, bool bWritten )
    {
        if ( bWritten == false )
            (void)_failedMask.fetch_or( static_cast<uint32>( kind ) );
        (void)_finishedMask.fetch_or( static_cast<uint32>( kind ) );
    }

    EngineFrameExit StartupCaptureSubsystem::update()
    {
        updateGameView();
        if ( gv_screenshotQuit == false )
            return kEngineFrameRunning;
        const uint32 expected = _expectedMask.load();
        const bool   bAllDone = expected != 0u && ( _finishedMask.load() & expected ) == expected;
        if ( bAllDone == false )
            return kEngineFrameRunning; // 요청한 사진이 하나도 없으면 끝낼 근거가 없다
        return _failedMask.load() != 0u ? ProcessExitCode::CaptureWriteFailed : ProcessExitCode::Passed;
    }

    void StartupCaptureSubsystem::updateGameView()
    {
        if ( gv_screenshot.empty() || _bGameViewFinished == SW_TRUE )
            return;
        if ( _bGameViewStarted == SW_FALSE )
        {
            _bGameViewStarted      = SW_TRUE;
            _waitStartMicroseconds = MonotonicClock::nowMicroseconds();
            expectCapture( StartupCaptureKind::GameView ); // 에디터 사진의 알림과 어느 쪽이 먼저든, 첫 사진(10 프레임 뒤)보다 앞선다
        }
        const uint32 shotTotal = gv_screenshotCount > 1 ? static_cast<uint32>( gv_screenshotCount ) : 1u;
        if ( _pendingPath.empty() == false && collectPendingShot() == false )
        {
            ++_frameCounter; // 쓰는 동안에도 시각은 흐른다 — 다음 장의 차례는 센 프레임으로 정한다
            return;
        }
        if ( _shotCount >= shotTotal )
        {
            finishGameView();
            return;
        }

        // 첫 장 전에 자리 잡음이 풀리면 처음부터 다시 센다 — N 프레임 내내 자리 잡은 그림을 찍는다. 시한을 넘기면 찍지 않고 실패로 끝낸다.
        const bool   bCountFromStable = gv_screenshotSettledFrames > 0;
        const uint32 firstFrame       = bCountFromStable                                                                    ? static_cast<uint32>( gv_screenshotSettledFrames )
                                      : gv_screenshotFrame > static_cast<int32>( StartupCaptureInternal::kMinWarmupFrames ) ? static_cast<uint32>( gv_screenshotFrame )
                                                                                                                            : StartupCaptureInternal::kMinWarmupFrames;
        if ( bCountFromStable && _shotCount == 0u && _frameCounter < firstFrame && isScreenStable() == false )
        {
            _frameCounter = 0;
            if ( MonotonicClock::nowMicroseconds() - _waitStartMicroseconds > StartupCaptureInternal::kStabilityTimeoutMicroseconds )
            {
                SW_LOG_ERROR_ID( "StartupCapture.ScreenNotSettled", "Screenshot was not taken: the screen did not settle within %# s (loading screen, screen fade or asset loads never finished) - %#",
                                 StartupCaptureInternal::kStabilityTimeoutSeconds, gv_screenshot.c_str() );
                _bGameViewFailed = SW_TRUE;
                finishGameView();
            }
            return;
        }
        // 장 k 의 차례는 첫 장 + k × 간격 프레임이다(앞 장 쓰기가 길었으면 늦은 만큼 곧바로 찍는다).
        const uint32 interval = gv_screenshotInterval > 1 ? static_cast<uint32>( gv_screenshotInterval ) : 1u;
        const uint32 frame    = ++_frameCounter;
        if ( frame >= firstFrame + _shotCount * interval )
            requestNextShot( shotTotal );
    }

    bool StartupCaptureSubsystem::isScreenStable()
    {
        const SceneManager* const      pSceneManager = engine::findSubsystem<SceneManager>();
        const Scene* const             pActiveScene  = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        const GameObjectManager* const pManager      = pActiveScene != nullptr ? pActiveScene->getObjectManager() : nullptr;
        // 첫 씬이 플레이를 시작하기 전(로딩 화면을 열기 전)은 자리 잡지 않았다 — 판정 자체는 편집 모드도 찍으므로 플레이를 보지 않는다.
        return pManager != nullptr && pManager->hasBegunPlay() && AutomationCaptureUtil::isReadyForCapture( pManager );
    }

    bool StartupCaptureSubsystem::collectPendingShot()
    {
        // shortcut: 실행 중 요청 창구는 하나라 이 실행에서 다른 쪽(개발 명령 screenshot · 에디터 버튼)이 같이 요청하면 결과를 섞어 읽을 수 있다 — 시작 인자 실행은 혼자 찍는다고 본다(자동화 `<Screenshot>` 은 패킷 창구라 섞이지 않는다).
        if ( RenderThread::getCompletedScreenshotSerial() == _pendingSerial )
            return false;
        if ( RenderThread::getLastScreenshotPath() != _pendingPath )
        {
            SW_LOG_ERROR_ID( "StartupCapture.ShotNotWritten", "Screenshot was not written: %#", _pendingPath.c_str() ); // 요청한 촬영이 빠졌다 - 실패다
            _bGameViewFailed = SW_TRUE;
        }
        _pendingPath.clear();
        return true;
    }

    void StartupCaptureSubsystem::requestNextShot( uint32 shotTotal )
    {
        string path{ gv_screenshot };
        if ( shotTotal > 1u )
        {
            StringBuilder<constant::buffer::kMax16> suffix;
            suffix.appendFormat( "_%03u", _shotCount );
            path = FileUtil::removeExtension( path ) + suffix.c_str() + FileUtil::getExtension( path );
        }
        ++_shotCount;
        // 중간 단계를 보고 싶다고 첨부 이름을 지정했으면 그 렌더 타깃을, 아니면 화면에 나간 이미지를 읽는다.
        const string_view attachment{ gv_screenshotAttachment };
        RenderTargetInfo  target{};
        if ( attachment.empty() == false && StartupCaptureInternal::findAttachment( attachment, target ) == false )
        {
            SW_LOG_ERROR_ID( "StartupCapture.AttachmentNotFound", "Screenshot '%#' was not taken: render target '%#' is not in the published render target list", path.c_str(), string( attachment ).c_str() );
            _bGameViewFailed = SW_TRUE;
            return;
        }
        _pendingSerial = RenderThread::getCompletedScreenshotSerial();
        _pendingPath   = std::move( path );
        RenderThread::requestScreenshot( _pendingPath, target._texture, target._format );
    }

    void StartupCaptureSubsystem::finishGameView()
    {
        _bGameViewFinished = SW_TRUE;
        reportCaptureFinished( StartupCaptureKind::GameView, _bGameViewFailed == SW_FALSE );
    }
} // namespace sw
