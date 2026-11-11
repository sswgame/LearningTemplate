/**
 * @file Engine/Renderer/EngineRenderDriver.h
 * @brief 엔진 루프의 렌더러 몫(프레임 패킷 제출 · 창 크기 · 백엔드 교체 · 캡처 종료)을 맡는 서브시스템입니다. `IEngineRenderDriver` 로 찾습니다.
 * @details 선언한 의존이 없습니다 — RHI 에 기대면 백엔드 교체(`shutdownDependents`)가 교체를 돌리는 자기를 내린다. 세 단계는 공장에서 빌리고 기동이 끝난 뒤에 읽습니다.
 *          전용 서버는 이 폴더를 짓지 않아 등록이 없고, 엔진 루프는 인터페이스가 없으면 건너뜁니다(docs/agents/BuildConfigSwitch.md 셋째 줄).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Graphics/RHI/RHIWaitIdleReason.h"
#include "Engine/Graphics/View/FrameViewports.h"
#include "Engine/Presentation/IEngineRenderDriver.h"

namespace sw
{
    enum class WindowLifecycleEvent : uint8;

    class FontService;
    class FrameRenderer;
    class FrameRendererSubsystem;
    class RenderPacketBuilder;
    class RenderThreadSubsystem;
    class RHISubsystem;
    class SceneManager;
    class SubsystemScope;
    class UISystem;

    /**
     * @class EngineRenderDriver
     * @brief `IEngineRenderDriver` 의 구현입니다. 수명은 `EngineRenderDriverSubsystem` 이 맡는다(서브시스템과 창구를 함께 상속하지 않는다 — 린트 `cpp-shape`).
     */
    class EngineRenderDriver final : public IEngineRenderDriver
    {
    public:
        /** @param startupScope 기동 범위입니다. 백엔드 교체가 그 컬렉션의 단계를 내리고 다시 세웁니다. 이 드라이버를 든 서브시스템의 주인이라 먼저 사라지지 않습니다. */
        EngineRenderDriver( SubsystemScope& startupScope, RHISubsystem& rhiSubsystem, FrameRendererSubsystem& frameRendererSubsystem, RenderThreadSubsystem& renderThreadSubsystem );

        void                        onStartupFinished() override;
        void                        onShutdownBegin() override;
        [[nodiscard]] RHI*          findRHI() const override;
        [[nodiscard]] RHIInitResult getRHIInitResult() const override;
        [[nodiscard]] RenderThread* findRenderThread() const override { return _pRenderThread; }
        void                        flushSubmittedPackets() override;
        void                        waitRenderThreadIdle( RHIWaitIdleReason reason ) override;
        void                        submitFrame( float32 deltaTime, const FrameViewports& views, const ViewCameraProviderDelegate& sceneViewCameraProvider ) override;
        [[nodiscard]] bool          applyPendingBackendChange() override;
        void                        setScenarioCaptureEnabled( bool bEnabled ) override;
        void                        setPresentInterval( uint32 interval ) override;
        [[nodiscard]] uint32        countPendingCaptureJobs( const GameObjectManager* pManager ) const override;

    private:
        /**
         * @brief 디바이스 재생성 뒤 내렸던 단계(렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬의 디바이스)를 다시 세웁니다. 모두 섰으면 true 입니다.
         * @param pOutTiming 주면 자원 다시 올리기 · 단계 다시 세우기에 든 시간을 한 줄로 받습니다(교체 로그).
         */
        [[nodiscard]] bool rebindSceneAfterDeviceRecreate( string* pOutTiming = nullptr );
        /**
         * @brief 주 창 크기가 바뀌었습니다 — 렌더 스레드를 쉬게 한 뒤 스왑체인 크기를 바꿉니다(창 스레드).
         * @details 창 · RHI · 렌더 스레드를 모두 가진 쪽의 일이다(언리얼 `FSceneViewport::ResizeFrame` · Godot RenderingServer 와 같은 자리). 호스트는 모른다.
         */
        void onMainWindowResize( uint32 width, uint32 height );
        /**
         * @brief 주 창의 수명 이벤트입니다 — 표면이 사라지기 전에 렌더 스레드가 받은 패킷을 끝내게 합니다(창 스레드).
         * @details shortcut: 스왑체인을 놓고 다시 만드는 일 · 오디오 멈춤은 빠져 있다. 표면이 실제로 사라지는 플랫폼(Android Vulkan)의 백엔드를 붙일 때 더한다.
         */
        void onMainWindowLifecycle( WindowLifecycleEvent event );

    private:
        SubsystemScope&         _startupScope;
        RHISubsystem&           _rhiSubsystem;
        FrameRendererSubsystem& _frameRendererSubsystem;
        RenderThreadSubsystem&  _renderThreadSubsystem;
        RHI*                    _pRHI;                         ///< `onStartupFinished` 가 잡는다(서지 않았으면 nullptr). 교체는 같은 RHI 객체의 디바이스만 바꾼다
        RenderThread*           _pRenderThread;                ///< 같다. 렌더 스레드 객체도 교체를 지나 그대로다
        FrameRenderer*          _pFrameRenderer;               ///< 프레임 패킷을 채울 때 빌려 쓴다
        RenderPacketBuilder*    _pPacketBuilder;               ///< 프레임 패킷을 채우는 GT 쪽 상태(씬 스냅샷 빌더 · 패킷 · 업로드 큐 · 추가 뷰 스케줄러)
        SceneManager*           _pSceneManager;                ///< 활성 씬 · 미리보기 씬을 패킷에 싣는다
        UISystem*               _pUISystem;                    ///< 캔버스를 패킷에 싣는다
        FontService*            _pFontService;                 ///< 캔버스가 글리프 캐시를 싣는다
        Subscription            _mainWindowResizeSubscription; ///< 주 창 크기 칸의 구독입니다(RHI 가 선 클라이언트만). `onShutdownBegin` 이 창보다 먼저 놓는다
        Subscription            _mainWindowLifecycleSubscription;
    };

    /**
     * @class EngineRenderDriverSubsystem
     * @brief `EngineRenderDriver` 를 멤버로 들고 기동 범위의 수명 짝을 맡는 서브시스템입니다. 기동이 그 멤버를 `IEngineRenderDriver` 로 건다(`SubsystemScope::exposeMember`).
     */
    class EngineRenderDriverSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName      = "EngineRenderDriver";
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::RenderCPU;

        /** @brief 인자는 `EngineRenderDriver` 의 것입니다. */
        EngineRenderDriverSubsystem( SubsystemScope& startupScope, RHISubsystem& rhiSubsystem, FrameRendererSubsystem& frameRendererSubsystem, RenderThreadSubsystem& renderThreadSubsystem );

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        SubsystemHostMask   getSubsystemHost() const override;
        /** @brief 받은 렌더 스레드 단계(그 아래 렌더러 · RHI)보다 늦게 서고 먼저 내린다 — 공장이 세 단계를 받아 든다(`EngineRegistrationList::findLifetimeMismatch`). */
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 공개할 드라이버입니다. */
        EngineRenderDriver& getRenderDriver() { return _driver; }

    private:
        EngineRenderDriver _driver;
    };
} // namespace sw
