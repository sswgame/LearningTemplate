/**
 * @file Engine/Presentation/IEngineRenderDriver.h
 * @brief 엔진 루프 · 호스트가 렌더러를 보는 유일한 창구입니다 — 구현은 렌더러 폴더(`EngineRenderDriver`)가 스스로 등록하고, 렌더러를 짓지 않는 구성(전용 서버)에는 없습니다.
 * @details 언리얼 `IRendererModule` · Godot `RenderingServer` 와 같은 자리입니다. 엔진 루프는 렌더러 형식을 하나도 알지 못하고 이 포인터가 nullptr 이면 그리는 일을 건너뜁니다.
 *          렌더러는 층 7, 이 헤더는 층 2(Presentation — `IEngineUIDriver` · `IEngineAudioDriver` 와 한 자리)라 렌더러가 이것을 구현하고 층 8(엔진 루프 · 자동화)이 부른다.
 *          렌더 스레드를 직접 다뤄야 하는 클라이언트 전용 호스트(App)만 `findRenderThread` 를 쓴다. 그래픽 형식(뷰 요청 · RHI 열거형)은 앞 선언 · 참조로만 받는다 — 층 2 는 Graphics(4)를 읽지 못한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/ViewCameraProvider.h"

namespace sw
{
    class GameObjectManager;
    class RenderThread;
    class RHI;
    struct FrameViewports;
    enum class RHIInitResult : uint8;
    enum class RHIWaitIdleReason : uint8;

    /**
     * @class IEngineRenderDriver
     * @brief 프레임 제출 · 백엔드 교체 · 창 크기 · 캡처 준비를 렌더러에 맡기는 인터페이스입니다. 게임 스레드에서만 부릅니다.
     */
    class IEngineRenderDriver
    {
    public:
        virtual ~IEngineRenderDriver() = default;

        /** @brief 기동 단계가 끝났습니다(실패했어도 부른다) — 선 RHI · 렌더 스레드를 잡고 주 창 크기를 따라가기 시작합니다. */
        virtual void onStartupFinished() = 0;
        /** @brief 종료를 시작합니다 — 창과 RHI 가 내려가기 전에 창 구독을 놓습니다. */
        virtual void onShutdownBegin() = 0;

        /** @brief 선 RHI 입니다(디바이스가 있습니다). 서지 않았으면(헤드리스 · 실패) nullptr 입니다. */
        [[nodiscard]] virtual RHI* findRHI() const = 0;
        /** @brief RHI 기동 단계의 결과입니다. 돌지 않았으면 `NotStarted` 입니다. */
        [[nodiscard]] virtual RHIInitResult getRHIInitResult() const = 0;
        /** @brief 렌더 스레드입니다(서지 않았으면 nullptr). 클라이언트 호스트만 씁니다 — 형식은 앞으로 선언뿐이고 쓰는 쪽이 렌더러 헤더를 읽습니다. */
        [[nodiscard]] virtual RenderThread* findRenderThread() const = 0;

        /** @brief 렌더 스레드가 받은 패킷을 다 그릴 때까지 기다립니다(GPU 는 기다리지 않는다 — 언리얼 FlushRenderingCommands). */
        virtual void flushSubmittedPackets() = 0;
        /** @brief 렌더 스레드와 GPU 를 쉬게 합니다(핫 리로드 · 종료 앞). */
        virtual void waitRenderThreadIdle( RHIWaitIdleReason reason ) = 0;

        /** @brief 머티리얼을 올리고 렌더 프레임 패킷을 채워 렌더 스레드에 냅니다. */
        virtual void submitFrame( float32 deltaTime, const FrameViewports& views, const ViewCameraProviderDelegate& sceneViewCameraProvider ) = 0;

        /** @brief 대기 중인 RHI 핫스왑을 수행합니다. 요청이 없으면 true, 교체에 실패하면(되돌렸어도) false 입니다. */
        [[nodiscard]] virtual bool applyPendingBackendChange() = 0;

        /** @brief 시나리오 실행의 사진 준비를 켭니다(`RenderThread::setScenarioCaptureEnabled`). */
        virtual void setScenarioCaptureEnabled( bool bEnabled ) = 0;
        /** @brief 그린 프레임 @p interval 개마다 하나만 내보냅니다(0 = 내보내지 않음 — 무인 실행, `RenderThread::setPresentInterval`). */
        virtual void setPresentInterval( uint32 interval ) = 0;
        /** @brief 씬 @p pManager 의 주 화면 캡처까지 남은 일 수입니다(`CaptureReadiness::countPendingJobs`). */
        [[nodiscard]] virtual uint32 countPendingCaptureJobs( const GameObjectManager* pManager ) const = 0;
    };
} // namespace sw
