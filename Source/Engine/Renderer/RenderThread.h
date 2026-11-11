/**
 * @file RenderThread.h
 * @brief 패킷을 전용 렌더 스레드에서, 또는 부르는 스레드에서 바로(inline) 실행합니다.
 * @details start() 로 띄우면 워커가 그래픽스 컨텍스트를 가지고 GT 는 submit() 만 합니다.
 *          bind() 만 하면(start 없음) submit() 이 부르는 스레드에서 executeInline 합니다.
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RHI/RHIWaitIdleReason.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"

#include <condition_variable>

namespace sw
{
    class FrameRenderer;
    class OffscreenSceneRenderers;
    class IRHICommandContext;
    class IRHIDevice;
    class Scene;

    /** @brief 렌더 스레드에서 실행할 일 하나입니다(`RenderThread::executeAndWait`). 디바이스는 그 스레드가 컨텍스트를 잡은 것입니다. */
    SW_DECLARE_DELEGATE( void, RenderCommandDelegate, IRHIDevice& );

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
         * @details submit() 이 부르는 스레드에서 executePacket 합니다. 렌더 쪽 객체 셋(디바이스 · 주 렌더러 · 화면 밖 씬 렌더러)은 붙는 동안 바뀌지 않는다 —
         *          스레드가 도는 중에 따로 바꾸는 세터를 두지 않는다(렌더 스레드가 읽는 포인터를 다른 스레드가 덮는 모양을 쓸 수 없게).
         */
        [[nodiscard]] bool bind( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer, OffscreenSceneRenderers* pOffscreenRenderers );
        /**
         * @brief 전용 워커를 띄웁니다. bindGraphicsContext 는 그 스레드에서 실행됩니다.
         */
        bool start( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer, OffscreenSceneRenderers* pOffscreenRenderers );
        /** @brief 워커를 멈춥니다. */
        void stop();
        /**
         * @brief 디바이스와 렌더러를 붙입니다. 전용 워커를 띄울지(`start`) 부르는 스레드에 붙일지(`bind`)는 `-gv_useRenderThread` 를 보고 정합니다.
         * @details 실행 중에 그 값이 바뀌면 `submit()` 이 모드를 그 자리에서 바꿉니다. 그래서 그 변수를 읽는 곳은 이 클래스뿐입니다.
         */
        [[nodiscard]] bool attach( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer, OffscreenSceneRenderers* pOffscreenRenderers );

        /**
         * @brief 워커가 있으면 링에 넣고, 없으면 executeInline 합니다.
         * @details 패킷은 **맞바꾸기**로 들어갑니다. 호출하는 쪽의 패킷은 링에 있던 지난 패킷(저장소 포함)을 돌려받습니다.
         *          그래서 호출하는 쪽이 패킷 하나를 스크래치로 가지고 매 프레임 다시 채우면 프레임당 할당이 없습니다. 주의: 옮겨
         *          넣기(move)로 받으면 링 슬롯의 저장소가 프레임마다 버려집니다.
         */
        void submit( RenderFramePacket& packet );
        /** @brief 워커가 돌고 있으면 RT 큐가 빌 때까지 기다리고 디바이스 waitIdle 까지 합니다. 워커가 없으면 할 일이 없습니다. @p reason 은 디바이스 대기에 그대로 넘깁니다(`RHIWaitIdleReason`). */
        void waitIdle( RHIWaitIdleReason reason );
        /** @brief 렌더 스레드가 받은 패킷을 모두 끝낼 때까지 기다립니다(장치 대기는 하지 않습니다). 렌더 스레드 자신이 부르면 곧바로 돌아옵니다. */
        void pollPackets();
        /** @brief 호출한 스레드에서 패킷 하나를 처리합니다. */
        void executeInline( RenderFramePacket& packet );
        /**
         * @brief @p command 를 렌더 스레드에서 실행하고 끝날 때까지 기다립니다(언리얼 `ENQUEUE_RENDER_COMMAND` + `FlushRenderingCommands`). 게임 스레드에서 부릅니다.
         * @details 받은 패킷을 먼저 끝낸 뒤 실행한다. 워커가 없으면(인라인 실행) 부른 스레드가 컨텍스트를 잡고 실행한다 — 그때는 그 스레드가 패킷을 실행하는 스레드다.
         *          렌더 쪽 객체(렌더러 · GPUScene · 머티리얼 프록시 · 디바이스 프레임)를 다른 스레드가 직접 만지지 않게 하는 길이다. 캡처(초상화 · 프로브 굽기)가 쓴다.
         *          예전에는 게임 스레드가 렌더 스레드를 세우고 컨텍스트를 빼앗아 직접 그렸다(`FrameRenderer::execute( scene )`) — 렌더 쪽 객체를 두 스레드가 만지는 모양이었다.
         * @return 실행했으면 true, 디바이스가 없거나 컨텍스트를 잡지 못했거나 멈추는 중이라 실행하지 못했으면 false 입니다(컨텍스트 실패 로그는 디바이스가 남긴다).
         */
        [[nodiscard]] bool executeAndWait( const RenderCommandDelegate& command );
        /**
         * @brief @p command 를 맡기고 기다리지 않습니다(어느 스레드에서나 — 언리얼 `ENQUEUE_RENDER_COMMAND` 를 flush 없이 쓰는 자리). 패킷을 실행하는 스레드가 다음 프레임을 기록하기 전에 맡긴 순서대로 실행합니다.
         * @details 렌더 쪽 객체(렌더러 · PSO 캐시 · 패스 자원)를 고치는 일을 게임 스레드가 렌더 스레드와 GPU 를 세우고(`waitIdle`) 직접 하지 않게 하는 길이다 — 옛 PSO · 트랜지언트는
         *          디바이스가 그것을 쓴 프레임의 GPU 완료 뒤로 미뤄 놓으므로(백엔드 해제 큐) 프레임 사이에 바꾸면 된다(셰이더 핫 리로드 · 파이프라인 바꾸기).
         *          명령이 잡은 객체는 다음 프레임까지 살아 있어야 한다 — 렌더러를 가진 서브시스템이 렌더 스레드보다 오래 산다.
         */
        void enqueueCommand( RenderCommandDelegate command );

        // 훅 세터 넷은 렌더 스레드가 받은 패킷을 모두 끝내기를 기다린 뒤(`pollPackets`) 패킷 실행 역할을 빌려 바꾼다. 패킷을 내는 쪽(게임 스레드)이 부르므로
        // 그동안 새 패킷이 들어오지 않는다 — 렌더 스레드가 훅을 부르는 중에 델리게이트를 덮는 일이 없다(언리얼도 렌더 스레드가 읽는 값은
        // `FlushRenderingCommands` 뒤나 렌더 명령으로만 바꾼다). 빈 델리게이트를 넘기면 훅을 뗀다 — 뗀 뒤에는 렌더 스레드가 그 훅의 주인을 다시 부르지 않는다.
        /**
         * @brief 씬 렌더 뒤, 백버퍼 패스를 열기 직전에 부를 훅을 정합니다 — 렌더 패스 밖이라 업로드(버퍼 · 텍스처 · bindless 등록)를 한다.
         * @details 열린 패스 안의 업로드는 Vulkan 이 패스를 닫아 그 뒤의 드로우를 버린다(전송 계약 3 — 업로드는 그리기 기록 전에). present 훅은 그리기만 한다.
         */
        void setPrepareHook( PresentHookDelegate hook );
        /** @brief 씬 렌더 뒤, Present 전에 열린 백버퍼 패스 안에서 부를 훅을 정합니다(패킷을 실행하는 스레드에서 불립니다). 그리기만 한다 — 업로드는 `setPrepareHook` 의 훅이 한다. */
        void setPresentHook( PresentHookDelegate hook );
        /** @brief Present 뒤에 부를 훅을 정합니다(멀티 뷰포트와 플랫폼 창 처리용). */
        void setPostPresentHook( PresentHookDelegate hook );
        /** @brief 프레임의 `beginFrame` 바로 앞 훅을 겁니다(렌더 스레드가 부른다). */
        void setBeginFrameHook( BeginFrameHookDelegate hook );

        /** @brief 워커가 돌고 있으면 true 를 반환합니다. */
        bool isRunning() const { return _bRunning.load( std::memory_order_acquire ); }
        /** @brief 디바이스가 붙어 있으면 true 를 반환합니다. */
        bool isBound() const { return _pDevice != nullptr; }
        /** @brief 붙은 디바이스입니다(없으면 nullptr). 렌더 쪽 일은 이 디바이스로 `executeAndWait` 안에서 한다. */
        IRHIDevice* getDevice() const { return _pDevice; }
        /** @brief 패킷을 그리는 주 렌더러입니다(없으면 nullptr). 렌더 스레드 객체 — `executeAndWait` 의 명령 안에서만 만진다. */
        FrameRenderer* getFrameRenderer() const { return _pFrameRenderer; }
        /** @brief 화면 밖 씬 렌더러입니다(없으면 nullptr — 미리보기를 그리지 않는다). 소유는 `FrameRendererSubsystem`. 렌더 스레드 객체 — `executeAndWait` 의 명령 안에서만 만진다. */
        OffscreenSceneRenderers* getOffscreenRenderers() const { return _pOffscreenRenderers; }
        /**
         * @brief 지난 프레임에 씬 키 @p sceneKey(0 = 주 화면, 미리보기 씬 키)를 그린 렌더러가 머티리얼 PSO 를 아직 만드는 중이라 빠뜨린 배치 묶음 수를 @p outCount 에 담습니다.
         * @details 지난 프레임에 그 씬을 그리지 않았으면 false 입니다. 어느 스레드에서 불러도 됩니다(캡처 준비 판정 `CaptureReadiness`).
         */
        [[nodiscard]] bool findPendingMaterialPSOBatchCount( uint32 sceneKey, uint32& outCount ) const;

        /** @brief 자동화 시나리오 동안 Present 캡처를 켜 둡니다(`<Screenshot>` 이 화면에 나간 이미지를 캡처합니다). 게임 스레드에서 부릅니다. */
        void setScenarioCaptureEnabled( bool bEnabled ) { _bScenarioCaptureEnabled.store( bEnabled, std::memory_order_release ); }
        /**
         * @brief 그린 프레임 @p interval 개마다 하나만 화면에 내보냅니다(0 = 내보내지 않음 — 무인 시나리오, 언리얼 자동화의 `-RenderOffScreen` 자리 · 1 = 매 프레임 · N = 지켜보는 시나리오의 배속). 게임 스레드에서 부릅니다.
         * @details 그리기 · Present 캡처(사진은 캡처 사본에서 읽는다)는 매 프레임 그대로라 그림이 바뀌지 않고, 내보내기(DWM 합성 · VSync 대기)만 준다 — 편집기 체인 92 → 64 초(Release, 0).
         *          세는 쪽은 렌더 스레드다(게임 스레드가 프레임마다 켜고 끄면 한 프레임 늦게 도는 렌더 스레드와 어긋난다).
         */
        void setPresentInterval( uint32 interval ) { _presentInterval.store( interval, std::memory_order_release ); }

        /**
         * @brief 다음에 그리는 프레임의 이미지를 @p filePath 에 씁니다. 확장자가 `.png` 면 PNG, 아니면 PPM 입니다. 어느 스레드에서 불러도 됩니다.
         * @details @p sourceTexture 가 0이면 화면에 나간 이미지(Present 결과이며, 에디터면 주 출력으로 게임 뷰가 보이면 게임 뷰, 아니면 씬 뷰)를 읽고,
         *          아니면 그 렌더 타깃(포맷 @p sourceFormat)을 읽습니다. 주 출력이 아닌 에디터 씬 뷰가 그 경우입니다.
         *          Present 캡처는 켠 프레임에 아직 비어 있을 수 있어서, 캡처를 켜고 한 프레임을 더 그린 뒤 `endFrame` 다음에 읽습니다.
         *          요청은 렌더 스레드 하나가 받습니다(엔진 범위의 우편함 서비스 `ScreenshotRequestQueue` — 우편함이 없는 호스트에서는 오류로 알리고 버립니다). 앞 요청이 아직 남아 있으면 덮어씁니다.
         */
        static void requestScreenshot( string_view filePath, RHITextureHandle sourceTexture = 0, RHIFormat sourceFormat = RHIFormat::R8G8B8A8_UNORM );
        /** @brief `requestScreenshot` 요청을 처리한 수입니다(성공과 실패 모두 셉니다). 값이 바뀌면 `getLastScreenshotPath` 가 그 결과를 가리킵니다. */
        static uint32 getCompletedScreenshotSerial();
        /** @brief 마지막으로 처리한 `requestScreenshot` 이 쓴 파일 경로입니다. 실패했으면 빈 문자열입니다. */
        static string getLastScreenshotPath();

    private:
        /** @brief 렌더 스레드 루프입니다. 패킷을 꺼내 executePacket 합니다. */
        void threadMain();
        /** @brief 패킷을 실행하고, 중단되더라도 postPresent 훅이 호출되도록 보장합니다. 패킷 실행 역할(`_packetThread`)의 진입점입니다. */
        void executePacket( RenderFramePacket& packet );
        /** @brief 지금 스레드(패킷을 실행하는 스레드)에서 명령 하나를 실행합니다. 패킷처럼 컨텍스트를 잡고 끝나면 놓습니다. */
        [[nodiscard]] bool runCommand( const RenderCommandDelegate& command );
        /** @brief 실행 스레드가 잡은 컨텍스트를 놓습니다(패킷 · 명령 하나가 끝날 때). */
        void releaseContextAfterJob();
        /**
         * @brief 씬 렌더부터 present 훅, Present 까지의 프레임 본문입니다.
         * @details 남은 본문은 프레임 단계의 순서(컨텍스트 → beginFrame → 게임 뷰 → 미리보기 → 백버퍼 → 훅 → Present → 촬영)가 곧 의미라 더 나누지 않는다.
         * @return 프레임을 실제로 그렸으면 true 이고, 유효하지 않은 패킷이나 리소스라 건너뛰었으면 false 입니다.
         */
        [[nodiscard]] bool executeFrameBody( RenderFramePacket& packet ) SW_REQUIRES_LOCK( _packetThread );
        /** @brief (패킷을 실행하는 스레드, 기록 전) `enqueueCommand` 로 맡긴 명령을 맡긴 순서대로 실행합니다. */
        void runQueuedCommands() SW_REQUIRES_LOCK( _packetThread );
        /**
         * @brief 에디터 미리보기 씬마다 자기 타깃을 열고 그린 뒤 읽기 상태로 둡니다. 렌더러가 준비 전이거나 프레임 스트림이 없으면 건너뜁니다.
         * @details 그린 뒤 씬마다(주 화면 = 키 0, @p bMainDrew 면) 빠뜨린 배치 수를 표에 다시 쓴다(`findPendingMaterialPSOBatchCount`).
         */
        void drawScenePreviews( RenderFramePacket& packet, IRHICommandContext* pFrameStream, bool bMainDrew );
        /**
         * @brief 화면에 나간 이미지(Present 캡처)를 GPU 에서 읽은 그대로(포맷 `constant::render::kBackBufferFormat`) 받습니다. 캡처가 없으면 @p path 와 함께 오류를 남기고 false 입니다.
         * @details `endFrame` 뒤에 부릅니다. RGBA 로 풀고 쓰는 일은 워커가 한다(`ScreenshotFileWrite`).
         */
        [[nodiscard]] bool readPresentedBytes( const string& path, vector<uint8>& outBytes, RHITextureMipSpan& outLayout );
        /** @brief 우편함의 `requestScreenshot` 요청을 이번 프레임의 작업으로 받습니다. 프레임 본문 앞에서 부릅니다. */
        void takeScreenshotRequest();
        /** @brief 받은 `requestScreenshot` 요청을 이번 프레임에 쓸 차례면 씁니다. `endFrame` 뒤에 부릅니다. */
        void writeRequestedScreenshot();
        /**
         * @brief 에디터 렌더 타깃 창이 `RenderTargetRegistry::requestReadback` 으로 요청한 타깃을 CPU 로 읽어 레지스트리에 보관합니다. `endFrame` 뒤에 부릅니다.
         * @details 읽기 티켓은 요청한 프레임에 기다리지 않고(RHIAsyncUploads U9) 이후 프레임의 호출이 `pollTransfer` 로 보고 `Complete` 일 때 가져갑니다. 읽는 중에는 새 요청을
         *          레지스트리에 둔 채 받지 않고(레지스트리가 최신 요청 하나만 쥔다), 스테이징이 차서 `Busy` 면 같은 요청을 다음 프레임에 다시 냅니다.
         */
        void readRequestedRenderTarget();
        /** @brief 아직 끝나지 않은 렌더 타깃 읽기를 버립니다(백엔드 교체 · 정지 전에 — 티켓이 팩토리보다 오래 살지 않게). */
        void discardPendingRenderTargetRead();
        /** @brief 지금 스레드에 그래픽스 컨텍스트가 있는지 확인합니다. 없으면 붙입니다. */
        [[nodiscard]] bool ensureContextOnCurrentThread();
        /** @brief `IRHIDevice::setRenderThreadFlush` 에 설정하는 함수입니다(`pContext` 는 이 객체입니다). */
        static void pollPacketsThunk( void* pContext );

    private:
        IRHIDevice*              _pDevice;
        FrameRenderer*           _pFrameRenderer;
        OffscreenSceneRenderers* _pOffscreenRenderers;
        vector<uint32>           _listPendingRetiredSceneKey; ///< 받은 패킷이 알린 사라진 미리보기 씬 키 — 내릴 수 있는 프레임까지 모은다(알림은 한 번뿐이라 그리지 않은 패킷에서 잃으면 렌더러가 남는다)
        /// @brief 패킷을 실행하는 스레드의 역할입니다(전용 워커면 그 스레드, 인라인이면 부른 스레드). 훅은 이 역할 아래에 있고 세터는 렌더 스레드를 비운 뒤 빌린다.
        ThreadRole                             _packetThread;
        PresentHookDelegate _prepareHook       SW_GUARDED_BY( _packetThread );
        PresentHookDelegate _presentHook       SW_GUARDED_BY( _packetThread );
        PresentHookDelegate _postPresentHook   SW_GUARDED_BY( _packetThread );
        BeginFrameHookDelegate _beginFrameHook SW_GUARDED_BY( _packetThread );
        sw::thread                             _thread;
        atomic<bool>                           _bRunning;
        StopSource                             _stop;          ///< 렌더 스레드를 멈추라는 신호(다시 시작하면 새 원천으로 바꾼다)
        atomic<bool>                           _bContextBound; ///< 지금 실행 스레드에서 bindGraphicsContext 가 성공했는지 나타냅니다.
        /// @brief 직전 프레임에 디바이스로 전달한 제출 정책입니다. 바뀔 때만 로그를 남기려고 둡니다.
        uint8          _bLastImmediateSubmit : 1;
        uint32         _budgetFrameCounter;      ///< 드라이버 GPU 메모리 값을 몇 프레임마다 물을지 센다(`_s_kBudgetRefreshFrames`).
        atomic<bool>   _bScenarioCaptureEnabled; ///< 자동화 시나리오 동안 Present 캡처를 켜 둔다(스크린샷 패킷이 언제 올지 모른다).
        atomic<uint32> _presentInterval;         ///< 그린 프레임 몇 개마다 하나를 내보내는가(`setPresentInterval` — 0 = 내보내지 않음)
        uint32         _presentFrameCounter;     ///< 내보내기 간격을 세는 렌더 스레드의 그린 프레임 수
        /** @brief 지난 프레임에 그린 씬 하나의 빠뜨린 배치 수입니다(`findPendingMaterialPSOBatchCount`). */
        struct ScenePendingJob
        {
            uint32 _sceneKey{ 0 };
            uint32 _pendingBatchCount{ 0 };
        };
        mutable mutex                                    _scenePendingMutex;                  ///< 아래 표를 지킨다(렌더 스레드가 프레임마다 쓰고 게임 스레드가 읽는다)
        array<ScenePendingJob, 1 + kMaxPreviewSceneView> _arrScenePending;                    ///< 지난 프레임에 그린 씬(주 화면 + 미리보기)마다의 값
        uint32                                           _scenePendingCount;                  ///< `_arrScenePending` 에 쓴 칸 수
        string                                           _requestedScreenshotPath;            ///< 받은 `requestScreenshot` 요청의 경로(렌더 스레드만 쓴다). 비면 요청이 없다.
        RHITextureHandle                                 _requestedScreenshotTexture;         ///< 0이면 Present 캡처이고, 아니면 그 렌더 타깃이다.
        RHIFormat                                        _requestedScreenshotFormat;          ///< `_requestedScreenshotTexture` 의 포맷
        uint32                                           _requestedScreenshotWaitFrames;      ///< 쓰기 전에 더 그릴 프레임 수(캡처를 켠 프레임은 비어 있을 수 있다)
        shared_ptr<atomic<uint32>>                       _pRequestedScreenshotWritesInFlight; ///< 워커가 쓰는 중인 실행 중 요청 수(잡이 같이 들어 이 객체보다 오래 살 수 있다). 0 이 아니면 다음 요청을 꺼내지 않는다
        struct PendingRenderTargetRead;
        unique_ptr<PendingRenderTargetRead> _pPendingTargetRead; ///< 요청을 받았고 읽기가 끝나지 않은 렌더 타깃 읽기(렌더 스레드만 쓴다). 없으면 비어 있다

        static constexpr uint32 _s_kRingCapacity{ constant::render::kRenderFrameQueueDepth };
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
        /// @brief `executeAndWait` 가 맡긴 명령입니다(받은 패킷을 끝낸 뒤 실행한다). 실행이 끝나면 비우고 `_cvIdle` 로 알린다. 맡기는 쪽은 게임 스레드 하나다.
        const RenderCommandDelegate* _pPendingCommand    SW_GUARDED_BY( _mutex );
        bool _bCommandSucceeded                          SW_GUARDED_BY( _mutex );
        mutex                                            _queuedCommandMutex;
        vector<RenderCommandDelegate> _listQueuedCommand SW_GUARDED_BY( _queuedCommandMutex ); ///< `enqueueCommand` 가 맡긴 명령(다음 프레임 기록 전에 비운다)
    };
} // namespace sw
