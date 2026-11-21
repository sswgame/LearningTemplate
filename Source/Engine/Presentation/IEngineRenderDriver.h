/**
 * @file Engine/Presentation/IEngineRenderDriver.h
 * @brief 엔진 루프 · 호스트가 렌더러를 보는 유일한 창구입니다 — 구현은 렌더러 폴더(`EngineRenderDriver`)가 스스로 등록하고, 렌더러를 짓지 않는 구성(전용 서버)에는 없습니다.
 * @details 언리얼 `IRendererModule` · Godot `RenderingServer` 와 같은 자리입니다. 엔진 루프는 렌더러 형식을 하나도 알지 못하고 이 포인터가 nullptr 이면 그리는 일을 건너뜁니다.
 *          렌더러는 층 7, 이 헤더는 층 2(Presentation — `IEngineUIDriver` · `IEngineAudioDriver` 와 한 자리)라 렌더러가 이것을 구현하고 층 8(엔진 루프 · 자동화)이 부른다.
 *          렌더 스레드를 직접 다뤄야 하는 클라이언트 전용 호스트(App)만 `findRenderThread` 를 쓴다. 그래픽 형식(뷰 요청 · RHI 열거형)은 앞 선언 · 참조로만 받는다 — 층 2 는 Graphics(4)를 읽지 못한다.
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/FrameDrawMode.h"
#include "Engine/Presentation/ViewCameraProvider.h"

namespace sw
{
    class GameObjectManager;
    class IRHIDevice;
    class RenderThread;
    class RHI;
    struct FrameViewports;
    enum class RHIDeviceLostPolicy : uint8;
    enum class RHIInitResult : uint8;
    enum class RHIWaitIdleReason : uint8;

    /**
     * @brief 예약된 장치 교체 · 다시 만들기(`IEngineRenderDriver::applyPendingBackendChange`)의 결과입니다. 장치를 쓸 수 있는지가 결과 안에 있다 — 부르는 쪽이 RHI 를 다시 묻지 않는다.
     * @details 모듈 관리자(`GameModuleManager::applyPendingDeviceChange`)가 이것으로 모듈을 다시 세울지, 끝낼지를 가른다.
     */
    enum class BackendChangeResult : uint8
    {
        Applied,  ///< 요청한 장치가 섰고 장면을 다시 묶었다(예약이 없었어도 이것이다)
        Restored, ///< 요청한 백엔드로 장치를 만들지 못해 앞 백엔드로 되돌렸다 — 장치는 서 있고 장면도 다시 묶었다
        Failed,   ///< 쓸 장치가 없다(되돌리지도 못했거나, 새 장치에 장면을 다시 묶지 못했다) — 모듈을 다시 세우지 않고 끝낸다
    };

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
        /** @brief 지금 그리는 RHI 장치입니다. 장치가 서기 전 · 잃은 뒤면 nullptr 입니다 — 렌더러 헤더 없이 장치를 읽는 쪽(메모리 스냅숏의 GPU 자원 표)이 쓴다. */
        [[nodiscard]] virtual const IRHIDevice* findDevice() const = 0;
        /** @brief 렌더 스레드입니다(서지 않았으면 nullptr). 클라이언트 호스트만 씁니다 — 형식은 앞으로 선언뿐이고 쓰는 쪽이 렌더러 헤더를 읽습니다. */
        [[nodiscard]] virtual RenderThread* findRenderThread() const = 0;

        /** @brief 렌더 스레드가 받은 패킷을 다 그릴 때까지 기다립니다(GPU 는 기다리지 않는다 — 언리얼 FlushRenderingCommands). */
        virtual void flushSubmittedPackets() = 0;
        /** @brief 렌더 스레드와 GPU 를 쉬게 합니다(핫 리로드 · 종료 앞). */
        virtual void waitRenderThreadIdle( RHIWaitIdleReason reason ) = 0;

        /** @brief 머티리얼을 올리고 렌더 프레임 패킷을 채워 렌더 스레드에 냅니다. */
        virtual void submitFrame( float32 deltaTime, const FrameViewports& views, const ViewCameraProviderDelegate& sceneViewCameraProvider, FrameDrawMode drawMode ) = 0;

        /** @brief 장치 교체 · 다시 만들기가 예약돼 있으면 true 입니다(`gv_rhiBackend` 변경 · 장치 분실의 `Recreate`). 프레임 경계에서 묻는다. */
        [[nodiscard]] virtual bool hasPendingBackendChange() const = 0;
        /** @brief 예약된 장치 교체 · 다시 만들기를 수행합니다. 모듈 인스턴스는 부르는 쪽이 이미 내렸다(`GameModuleManager::applyPendingDeviceChange`). */
        [[nodiscard]] virtual BackendChangeResult applyPendingBackendChange() = 0;

        /**
         * @brief 렌더러의 뷰 대상(풀 · TAA · 노출 · 깊이 기록 · 지터 순번)을 모두 버립니다 — 다음 프레임은 새 프로세스처럼 기록 없이 그린다(`FrameRenderer::discardViewTargets`).
         * @details 자동화 체인의 단계 시작이 부른다 — 앞 단계가 그린 기록이 다음 단계의 그림을 체인 위치에 따라 바꾸지 않게. 이미 낸 패킷을 끝낸 뒤 렌더 스레드가 다음 프레임 앞에서 버린다.
         */
        virtual void discardViewTargets() = 0;
        /** @brief 시나리오 실행의 사진 준비를 켭니다(`RenderThread::setScenarioCaptureEnabled`). */
        virtual void setScenarioCaptureEnabled( bool bEnabled ) = 0;
        /** @brief 그린 프레임 @p interval 개마다 하나만 내보냅니다(0 = 내보내지 않음 — 무인 실행, `RenderThread::setPresentInterval`). */
        virtual void setPresentInterval( uint32 interval ) = 0;
        /** @brief 씬 @p pManager 의 주 화면 캡처까지 남은 일 수입니다(`CaptureReadiness::countPendingJobs`). */
        [[nodiscard]] virtual uint32 countPendingCaptureJobs( const GameObjectManager* pManager ) const = 0;
        /**
         * @brief 장치 분실 정책을 설정 값(`EngineConfig::_deviceLostPolicy`) 대신 @p policy 로 덮습니다. 개발 구성의 시나리오 입구(`-gv_deviceLostPolicy` — 개발 도구의 `DeviceLostPolicySwitch`)만 부른다.
         * @details 기동 순서와 상관없이 이긴다 — 설정 읽기(`onStartupFinished`)는 덮은 값을 지우지 않는다. `None` 은 받지 않는다(덮지 않음은 부르지 않는 것이다).
         */
        virtual void overrideDeviceLostPolicy( RHIDeviceLostPolicy policy ) = 0;
        /** @brief 잃은 장치를 다시 만들어 장면을 다시 묶은 횟수입니다(`RHIDeviceLostPolicy::Recreate`). 감시 값 `Render.DeviceRecreateCount` 만 읽는 개발 전용 칸(배포는 0)이다. */
        [[nodiscard]] virtual const DevCounter& getDeviceRecreateCount() const = 0;
        /** @brief 요청한 백엔드로 장치를 만들지 못한 교체 횟수입니다(되돌렸든 못 했든 — `BackendChangeResult::Applied` 가 아닌 것). 감시 값 `RHI.SwapFailedCount`. */
        [[nodiscard]] virtual const DevCounter& getFailedBackendChangeCount() const = 0;
    };
} // namespace sw
