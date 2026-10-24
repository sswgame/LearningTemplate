/**
 * @file RenderThread.h
 * @brief 패킷을 전용 렌더 스레드에서, 또는 부르는 스레드에서 바로(inline) 실행합니다.
 * @details start() 로 띄우면 워커가 그래픽스 컨텍스트를 가지고 GT 는 submit() 만 합니다.
 *          bind() 만 하면(start 없음) submit() 이 부르는 스레드에서 executeInline 합니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"

#include <condition_variable>

namespace sw
{
    class FrameRenderer;
    class IRHICommandContext;
    class IRHIDevice;
    class Scene;

    /**
     * @class RenderThread
     * @details RHI 그리기와 Present 를 워커(start)에서, 또는 submit() 안에서 바로 실행합니다.
     *          패킷을 실행하는 스레드에서 bindGraphicsContext 를 부릅니다.
     */
    class SW_API RenderThread
    {
    public:
        /** @brief 워커 없이 만듭니다. bind 나 start 로 붙입니다. */
        RenderThread();
        /** @brief 스레드를 멈추고 조인합니다. */
        ~RenderThread();

        /** @brief 복사를 금지합니다. */
        RenderThread( const RenderThread& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderThread& operator=( const RenderThread& ) = delete;

        /**
         * @brief 워커 없이 디바이스와 렌더러를 붙입니다(GT 에서 바로 실행합니다).
         * @details submit() 이 부르는 스레드에서 executePacket 합니다.
         */
        bool bind( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer );
        /**
         * @brief 전용 워커를 띄웁니다. bindGraphicsContext 는 그 스레드에서 실행됩니다.
         */
        bool start( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer );
        /** @brief 워커를 멈춥니다. */
        void stop();
        /**
         * @brief 디바이스와 렌더러를 붙입니다. 전용 워커를 띄울지(`start`) 부르는 스레드에 붙일지(`bind`)는 `-gv_useRenderThread` 를 보고 정합니다.
         * @details 실행 중에 그 값이 바뀌면 `submit()` 이 모드를 그 자리에서 바꿉니다. 그래서 그 변수를 읽는 곳은 이 클래스뿐입니다.
         */
        [[nodiscard]] bool attach( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer );

        /**
         * @brief 워커가 있으면 링에 넣고, 없으면 executeInline 합니다.
         * @details 패킷은 **맞바꾸기**로 들어갑니다. 호출하는 쪽의 패킷은 링에 있던 지난 패킷(저장소 포함)을 돌려받습니다.
         *          그래서 호출하는 쪽이 패킷 하나를 스크래치로 가지고 매 프레임 다시 채우면 프레임당 할당이 없습니다. 주의: 옮겨
         *          넣기(move)로 받으면 링 슬롯의 저장소가 프레임마다 버려집니다.
         */
        void submit( RenderFramePacket& packet );
        /** @brief 워커가 돌고 있으면 RT 큐가 빌 때까지 기다리고 디바이스 waitIdle 까지 합니다. 워커가 없으면 할 일이 없습니다. */
        void waitIdle();
        /** @brief 렌더 스레드가 받은 패킷을 모두 끝낼 때까지 기다립니다(장치 대기는 하지 않습니다). 렌더 스레드 자신이 부르면 곧바로 돌아옵니다. */
        void drainPackets();
        /** @brief 호출한 스레드에서 패킷 하나를 처리합니다. */
        void executeInline( RenderFramePacket& packet );

        /** @brief 씬 렌더 뒤, Present 전에 부를 훅을 정합니다(패킷을 실행하는 스레드에서 불립니다). */
        void setPresentHook( PresentHookDelegate hook ) { _presentHook = std::move( hook ); }
        /** @brief Present 뒤에 부를 훅을 정합니다(멀티 뷰포트와 플랫폼 창 처리용). */
        void setPostPresentHook( PresentHookDelegate hook ) { _postPresentHook = std::move( hook ); }

        /** @brief 워커가 돌고 있으면 true 를 반환합니다. */
        bool isRunning() const { return _bRunning.load( std::memory_order_acquire ); }
        /** @brief 디바이스가 붙어 있으면 true 를 반환합니다. */
        bool isBound() const { return _pDevice != nullptr; }

        /** @brief 자동화 시나리오 동안 Present 캡처를 켜 둡니다(`<Screenshot>` 이 화면에 나간 이미지를 캡처합니다). 게임 스레드에서 부릅니다. */
        void setScenarioCaptureEnabled( bool bEnabled ) { _bScenarioCaptureEnabled.store( bEnabled, std::memory_order_release ); }
        /** @brief 패킷의 스크린샷 요청을 처리한 수입니다(게임 스레드가 읽습니다). */
        uint32 getCompletedScenarioScreenshotCount() const { return _completedScenarioScreenshotCount.load( std::memory_order_acquire ); }

        /**
         * @brief 다음에 그리는 프레임의 이미지를 @p filePath 에 씁니다. 확장자가 `.png` 면 PNG, 아니면 PPM 입니다. 어느 스레드에서 불러도 됩니다.
         * @details @p sourceTexture 가 0이면 화면에 나간 이미지(Present 결과이며, 에디터면 주 출력으로 게임 뷰가 보이면 게임 뷰, 아니면 씬 뷰)를 읽고,
         *          아니면 그 렌더 타깃(포맷 @p sourceFormat)을 읽습니다. 주 출력이 아닌 에디터 씬 뷰가 그 경우입니다.
         *          Present 캡처는 켠 프레임에 아직 비어 있을 수 있어서, 캡처를 켜고 한 프레임을 더 그린 뒤 `endFrame` 다음에 읽습니다.
         *          요청은 렌더 스레드 하나가 받습니다(엔진 범위의 우편함 서비스 `ScreenshotMailbox` — 우편함이 없는 호스트에서는 오류로 알리고 버립니다). 앞 요청이 아직 남아 있으면 덮어씁니다.
         */
        static void requestScreenshot( string_view filePath, RHITextureHandle sourceTexture = 0, RHIFormat sourceFormat = RHIFormat::R8G8B8A8_UNORM );
        /** @brief `requestScreenshot` 요청을 처리한 수입니다(성공과 실패 모두 셉니다). 값이 바뀌면 `getLastScreenshotPath` 가 그 결과를 가리킵니다. */
        static uint32 getCompletedScreenshotSerial();
        /** @brief 마지막으로 처리한 `requestScreenshot` 이 쓴 파일 경로입니다. 실패했으면 빈 문자열입니다. */
        static string getLastScreenshotPath();

        /** @brief 시작 인자(`-gv_screenshot` · `-gv_screenshotEditor`)가 찍는 사진의 종류입니다. `-gv_screenshotQuit` 은 요청한 종류가 모두 끝나야 앱을 끝냅니다. */
        enum class CaptureKind : uint32
        {
            GameView = 1u << 0, ///< `-gv_screenshot` — 렌더 스레드가 찍는다.
            EditorUI = 1u << 1, ///< `-gv_screenshotEditor` — 에디터가 찍는다.
        };
        /** @brief `-gv_screenshotQuit` 이 앱을 끝낼 때, 사진 파일을 쓰지 못한 경우의 종료 코드입니다(다 썼으면 0). */
        static constexpr int32 kCaptureWriteFailedExitCode = 14;
        /** @brief `getCaptureExitCode` 가 "아직 끝낼 때가 아니다" 로 돌려주는 값입니다. */
        static constexpr int32 kNoCaptureExitCode = -1;
        /** @brief 이 종류의 사진을 찍겠다고 알립니다. 첫 사진이 끝나기 전(첫 프레임)에 부릅니다. 어느 스레드에서 불러도 됩니다. */
        static void expectCapture( CaptureKind kind );
        /** @brief 이 종류의 사진 요청을 모두 처리했음을 알립니다(@p bWritten 이 false 면 파일을 쓰지 못했다). 어느 스레드에서 불러도 됩니다. */
        static void reportCaptureFinished( CaptureKind kind, bool bWritten );
        /**
         * @brief `-gv_screenshotQuit` 이 켜져 있고 요청한 사진을 모두 처리했으면 앱의 종료 코드(0 또는 `kCaptureWriteFailedExitCode`), 아니면 `kNoCaptureExitCode` 입니다.
         * @details 게임 스레드가 프레임마다 물어 `Engine::requestQuit` 으로 잇습니다. 사진을 요청한 종류가 하나도 없으면 끝낼 근거가 없어 늘 `kNoCaptureExitCode` 입니다.
         */
        [[nodiscard]] static int32 getCaptureExitCode();
        /**
         * @brief 시작 인자 캡처가 화면의 자리 잡음을 기다리는 중이면(`-gv_screenshot` 과 `-gv_screenshotSettledFrames`) true 입니다.
         * @details 게임 스레드가 패킷을 채울 때 이것이 참이면 `RenderFramePacket::_bReadyForCapture` 를 판정해 싣는다(그 판정은 텍스처 표를 훑으므로 필요할 때만 한다).
         */
        [[nodiscard]] static bool needsCaptureReadiness();

    private:
        /** @brief 렌더 스레드 루프입니다. 패킷을 꺼내 executePacket 합니다. */
        void threadMain();
        /** @brief 패킷을 실행하고, 중단되더라도 postPresent 훅이 호출되도록 보장합니다. */
        void executePacket( RenderFramePacket& packet );
        /**
         * @brief 씬 렌더부터 present 훅, Present 까지의 프레임 본문입니다.
         * @details 남은 본문은 프레임 단계의 순서(컨텍스트 → beginFrame → 게임 뷰 → 미리보기 → 백버퍼 → 훅 → Present → 촬영)가 곧 의미라 더 나누지 않는다.
         * @return 프레임을 실제로 그렸으면 true 이고, 유효하지 않은 패킷이나 리소스라 건너뛰었으면 false 입니다.
         */
        bool executeFrameBody( RenderFramePacket& packet );
        /** @brief 에디터 미리보기 씬마다 자기 타깃을 열고 그린 뒤 읽기 상태로 둡니다. 렌더러가 준비 전이거나 프레임 스트림이 없으면 건너뜁니다. */
        void drawScenePreviews( RenderFramePacket& packet, IRHICommandContext* pFrameStream );
        /** @brief `-gv_screenshot` 촬영 차례(대기 프레임 · 자리 잡음 · 간격 · 장 수)면 화면이나 지정 첨부를 파일로 씁니다. `endFrame` 뒤에 부릅니다. */
        void takeCommandLineScreenshotIfDue( const RenderFramePacket& packet );
        /** @brief 화면에 나간 이미지(Present 캡처, 없으면 Present 가 읽는 첨부)를 @p path 에 씁니다(`.png` 면 PNG, 아니면 PPM). `endFrame` 뒤에 부릅니다. */
        [[nodiscard]] bool writePresentedImage( const string& path );
        /** @brief 우편함의 `requestScreenshot` 요청을 이번 프레임의 작업으로 받습니다. 프레임 본문 앞에서 부릅니다. */
        void takeScreenshotRequest();
        /** @brief 받은 `requestScreenshot` 요청을 이번 프레임에 쓸 차례면 씁니다. `endFrame` 뒤에 부릅니다. */
        void writeRequestedScreenshot();
        /** @brief 에디터 렌더 타깃 창이 `RenderTargetRegistry::requestReadback` 으로 요청한 타깃을 CPU 로 읽어 레지스트리에 보관합니다. `endFrame` 뒤에 부릅니다. */
        void readRequestedRenderTarget();
        /** @brief 지금 스레드에 그래픽스 컨텍스트가 있는지 확인합니다. 없으면 붙입니다. */
        bool ensureContextOnCurrentThread();
        /** @brief `IRHIDevice::setRenderThreadDrain` 에 설정하는 함수입니다(`pContext` 는 이 객체입니다). */
        static void drainPacketsThunk( void* pContext );

    private:
        IRHIDevice*         _pDevice;
        FrameRenderer*      _pFrameRenderer;
        PresentHookDelegate _presentHook;
        PresentHookDelegate _postPresentHook;
        sw::thread          _thread;
        atomic<bool>        _bRunning;
        atomic<bool>        _bStop;
        atomic<bool>        _bContextBound; ///< 지금 실행 스레드에서 bindGraphicsContext 가 성공했는지 나타냅니다.
        /// @brief 직전 프레임에 디바이스로 전달한 제출 정책입니다. 바뀔 때만 로그를 남기려고 둡니다.
        bool             _bLastImmediateSubmit;
        uint8            _bScreenshotTaken;                 ///< -gv_screenshot 은 한 장만 찍는다.
        uint8            _bScreenshotWriteFailed;           ///< -gv_screenshot 의 장 중 쓰지 못한 것이 있다(`-gv_screenshotQuit` 의 종료 코드).
        uint32           _screenshotFrameCounter;           ///< 씬이 채워질 때까지 몇 프레임을 기다린다(`-gv_screenshotSettledFrames` 면 자리 잡은 패킷부터 이어 센다).
        uint32           _screenshotShotCount;              ///< 지금까지 찍은 장 수(`-gv_screenshotCount` 연속 촬영)
        int64            _screenshotWaitStartMicroseconds;  ///< `-gv_screenshot` 을 처음 본 단조 시각(0 = 아직). 자리 잡음 기다림의 시한을 측정한다.
        uint32           _budgetFrameCounter;               ///< 드라이버 GPU 메모리 값을 몇 프레임마다 물을지 센다(`_s_kBudgetRefreshFrames`).
        atomic<uint32>   _completedScenarioScreenshotCount; ///< 패킷이 요청한 스크린샷을 처리한 수(쓰지 못했어도 센다. 시나리오가 기다림을 끝낸다)
        atomic<bool>     _bScenarioCaptureEnabled;          ///< 자동화 시나리오 동안 Present 캡처를 켜 둔다(스크린샷 패킷이 언제 올지 모른다).
        string           _requestedScreenshotPath;          ///< 받은 `requestScreenshot` 요청의 경로(렌더 스레드만 쓴다). 비면 요청이 없다.
        RHITextureHandle _requestedScreenshotTexture;       ///< 0이면 Present 캡처이고, 아니면 그 렌더 타깃이다.
        RHIFormat        _requestedScreenshotFormat;        ///< `_requestedScreenshotTexture` 의 포맷
        uint32           _requestedScreenshotWaitFrames;    ///< 쓰기 전에 더 그릴 프레임 수(캡처를 켠 프레임은 비어 있을 수 있다)

        static constexpr uint32 _s_kRingCapacity{ constant::kRenderFrameQueueDepth };
        /// @brief 드라이버 GPU 메모리 값(`IRHIDevice::refreshGPUMemoryBudget`)을 묻는 간격입니다. DXGI 질의는 커널을 거치므로 프레임마다 묻지 않습니다.
        static constexpr uint32 _s_kBudgetRefreshFrames{ 30 };
        // 게임 스레드와 렌더 스레드가 이 링을 **동시에** 만진다. 그래서 레이스 탐지기가 붙은
        // sw::array 를 쓰지 않는다. 이유는 Core/Container/array.h 머리말에 있다.
        std::array<RenderFramePacket, _s_kRingCapacity> _arrRingBuffer;
        atomic<uint32>                                  _head; ///< 생산자(GT)가 쓴다.
        atomic<uint32>                                  _tail; ///< 소비자(RT)가 쓴다.

        mutex                       _mutex;
        std::condition_variable_any _cvProduce;
        std::condition_variable_any _cvConsume;
        std::condition_variable_any _cvIdle;
    };
} // namespace sw
