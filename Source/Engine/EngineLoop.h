/**
 * @file EngineLoop.h
 * @brief 엔진 코어의 메인 루프와 서브시스템 소유권을 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

#include "Engine/EngineBootstrap.h"
#include "Engine/EngineInitSequence.h"
#include "Engine/EngineServiceCollection.h"
#include "Engine/Graphics/Renderer/Frame/PresentHookDelegate.h"
#include "Engine/Utility/Debug/FrameProfileSession.h"
#include "Engine/Utility/Debug/MemoryBudgetMonitor.h"

namespace sw
{
    struct DebugOverlayState;
    struct EngineConfig;
    struct EngineDefaultAssets;
    struct RenderFramePacket;

    class AssetManager;
    class AssetStreamingQueue;
    class CameraComponent;
    class CommandLineManager;
    class CommandStack;
    class CompressionCodecRegistry;
    class ConfigManager;
    class DebugDrawQueue;
    class EventDispatcher;
    class FrameProfiler;
    class FrameRenderer;
    class GlobalVariableManager;
    class GpuSceneBuilder;
    class GpuUploadQueue;
    class IAudioSystem;
    class InputManager;
    class InputMap;
    class IRHIDevice;
    class RenderTargetRegistry;
    class RenderThread;
    class RHI;
    class RHIBackendRegistry;
    class SceneManager;
    class ShaderRecompiler;
    class TaskManager;
    class TypeRegistry;

    /**
     * @brief 이번 프레임의 뷰 카메라를 반환합니다.
     * @details tick 안의 핫 리로드 · 씬 전환이 GameObject 를 파괴할 수 있으므로,
     *          카메라는 미리 잡아 두지 않고 파괴 단계가 끝난 뒤 이 델리게이트로 조회합니다.
     */
    SW_DECLARE_DELEGATE( CameraComponent*, ViewCameraProviderDelegate, void );

    /**
     * @brief 호스트가 동적으로 올리는 타입 공급자(GameFramework · 킷 · 게임 모듈)를 올려 그 타입을 등록합니다. 실패하면 false 입니다.
     * @details 기동 단계 `ModuleTypes` 가 부릅니다. 이것이 돌아온 뒤 엔진은 "모든 타입이 등록됐다" 고 적고(`TypeRegistry::markAllModuleTypesRegistered`),
     *          그때부터 씬을 읽을 수 있습니다.
     */
    SW_DECLARE_DELEGATE( bool, ModuleTypeLoaderDelegate, void );

    /**
     * @class EngineLoop
     * @brief 코어 매니저들을 소유하고 메인 루프(tick)를 돌립니다.
     */
    class SW_API EngineLoop
    {
    public:
        EngineLoop();
        ~EngineLoop();

        EngineLoop( const EngineLoop& )            = delete;
        EngineLoop& operator=( const EngineLoop& ) = delete;

        /** @brief 서브시스템(창, RHI 포함)을 초기화합니다. */
        bool initialize( int32 argc, utf8* pArgv[] );
        /** @brief 매니저들을 종료하고 정리합니다. */
        void shutdown();

        /**
         * @brief 입력 등을 시작하는 프레임의 첫 단계입니다.
         * @param deltaSeconds 지난 프레임의 실제 시간(초). 입력 장치의 타이머(진동 길이 · 게임패드 재연결 주기)가 이 값으로 흐릅니다.
         *                     고정값으로 흘리면 0.3 초 진동이 144 fps 에서 0.13 초로 끝납니다.
         */
        void beginFrame( float32 deltaSeconds );
        /**
         * @brief 씬 업데이트, RHI 제출 등을 수행합니다.
         * @param deltaTime 델타 타임
         * @param gameRenderTarget 오프스크린 Game View RT 식별자 (없으면 0 = 백버퍼)
         * @param vpWidth 뷰포트 너비
         * @param vpHeight 뷰포트 높이
         * @param viewCameraProvider 호스트가 지정한 렌더 카메라를 돌려주는 델리게이트.
         *                           바인딩되지 않았거나 nullptr을 돌려주면 씬의 게임 카메라를 씁니다.
         * @param bTickScene false이면 씬 GameObject tick을 건너뜁니다 (에디터 Pause).
         */
        void tick( float32 deltaTime, uint64 gameRenderTarget, uint32 vpWidth, uint32 vpHeight,
                   const ViewCameraProviderDelegate& viewCameraProvider, bool bTickScene );
        /** @brief 입력 종료 등 프레임의 마지막 단계입니다. */
        void endFrame();

        /** @brief 대기 중인 RHI 핫스왑을 수행합니다. */
        [[nodiscard]] bool applyPendingBackendChange();

        // ----------------------------------------------------------------------
        // 도우미
        // ----------------------------------------------------------------------
        /**
         * @brief 씬이 모두 정리됐고 엔진 서비스는 **아직 살아 있는** 지점에 불릴 훅을 겁니다.
         * @details 종료 시퀀스에는 "씬은 사라졌지만 서비스(SceneManager · TaskManager · 로거)는 아직 있다" 는 좁은
         *          구간이 있습니다. 모듈 DLL 을 내리는 일은 정확히 거기서 일어나야 합니다. 더 일찍이면 씬이 든 컴포넌트
         *          팩토리 델리게이트가 사라진 코드를 가리키고, 더 늦으면 언로드가 쓰는 서비스와 로거가 이미 없습니다.
         *          Engine 은 그것이 무엇인지 모릅니다. 자리만 내주고, 무엇을 할지는 훅을 건 쪽이 정합니다.
         */
        void setOnScenesReleased( Delegate<void()> onScenesReleased );
        /**
         * @brief 기동 단계 `ModuleTypes` 가 부를 타입 공급자 로더를 겁니다. `initialize` **전에** 겁니다.
         * @details 걸지 않으면 호스트에 따로 올릴 타입 공급자가 없다는 뜻입니다(정적 링크 — 리플렉션 단계가 이미 다 모았다). 어느 쪽이든 단계가 끝나면
         *          모든 타입이 등록된 것으로 적고, 씬을 읽는 일(헤드리스 쿠킹 · 게임 · 에디터의 씬 로드)은 그 뒤에만 됩니다.
         */
        void setModuleTypeLoader( ModuleTypeLoaderDelegate moduleTypeLoader );

        void setPresentHook( sw::PresentHookDelegate presentHook );
        void setPostPresentHook( sw::PresentHookDelegate postPresentHook );
        void updateShellActions( float32 deltaTime );
        /**
         * @brief 셸 디버그 액션 맵을 InputMap 리소스(`EngineDefaultAssets::_shellInputMap`)에서 만듭니다.
         * @details 경로가 비었거나 읽지 못하면 오류를 남기고 **빈 맵**을 돌려줍니다. 손으로 적은 바인딩으로 바꿔 끼우지 않습니다 —
         *          그 내용은 리소스와 따로 낡고, 실패를 가립니다.
         */
        static unique_ptr<InputMap> createShellInputMap( string_view inputMapPath );

        /**
         * @brief 엔진이 스스로 종료를 원하면 true 입니다(`-gv_profileFrames=N` 을 다 채운 경우).
         * @details 창 수명은 App 이 쥐고 있으므로 여기서는 의사만 알립니다.
         */
        bool isQuitRequested() const { return _profileSession.isQuitRequested(); }

        /**
         * @brief 셸 디버그 InputMap 에서 해당 액션이 이번 프레임에 발동했는지 반환합니다.
         * @details Engine 이 내주는 것은 **입력 사실**뿐입니다. 그 액션이 무엇을 뜻하는지(모듈을 다시 올린다,
         *          에디터를 다시 올린다)는 그 장치를 가진 쪽이 정합니다 — Engine 이 리로드 콜백을 들면 Shipping 헤더에도
         *          리로드 델리게이트가 남습니다.
         */
        bool wasDebugActionTriggered( string_view actionName ) const;

        // ----------------------------------------------------------------------
        // Getter (App 이 ModuleHost 등과 연동하는 데 필요)
        // ----------------------------------------------------------------------
        ConfigManager*      getConfigManager() const { return _configManager.get(); }
        CommandLineManager* getCommandLineManager() const { return _owned._pCommandLineManager.get(); }
        RHI*                getRhi() const { return _rhi.get(); }
        RenderThread*       getRenderThread() const { return _renderThread.get(); }
        bool                isHeadless() const { return _bHeadless; }
        /** @brief 헤드리스 작업(셰이더 쿠킹 · 씬 쿠킹)이 실패했는지 반환합니다. 부르는 쪽은 이것을 종료 코드로 내보냅니다. */
        bool didHeadlessTaskFail() const { return _bHeadlessTaskFailed; }

    private:
        /** @brief 디바이스 재생성 뒤 내렸던 단계(렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬의 디바이스)를 다시 세웁니다. 모두 섰으면 true 입니다. */
        [[nodiscard]] bool rebindSceneAfterDeviceRecreate();
        /** @brief 셰이더 라이브 리로드 매니저입니다. Shipping 에서는 늘 nullptr 입니다. */
        ShaderRecompiler* getShaderRecompiler() const;
        /** @brief 셰이더 강제 리로드 핫키를 처리합니다. Engine 자신의 개발 도구이므로 여기서 끝냅니다. */
        void pollShaderReloadHotkey();

        // 기동 단계의 본문이다(`EngineLoop.cpp`). 표(`EngineInitStepList.xxx`)의 줄마다 `<단계>StartupStep` 하나이고, 빠지면 컴파일 오류다.
        // 중첩 타입이라 이 클래스의 private 을 그대로 쓰고, 바깥에서는 본문 표(`EngineInitStepTable`)만 이름을 본다.
#define SW_ENGINE_STARTUP_STEP( Name, ... ) struct Name##StartupStep;
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        template <class>
        friend struct EngineInitStepTable;

    private:
        /**
         * @brief 목록(`EngineServiceList.xxx`)의 `EngineCreated` 서비스 저장소입니다. 생성 · 바인딩이 여기서 나옵니다.
         * @details 목록에 줄을 더하면 이 저장소가 같이 자랍니다. 만드는 방법이 특별한 셋
         *          (팩토리 · 구성별 조건부)만 아래에 손으로 남아 있습니다.
         */
        EngineServiceCollection _owned;

        /** @brief 기동 표 밖의 부트스트랩(로거 · 크래시 핸들러 · 진단 도구 · 명령줄 · 전역 변수)입니다. 시험 하네스와 같은 것을 쓴다. `_owned` 보다 먼저 사라진다. */
        EngineBootstrap           _bootstrap;
        unique_ptr<ConfigManager> _configManager;
        unique_ptr<RHI>           _rhi;
        unique_ptr<InputMap>      _mapDebugAction;
        unique_ptr<IAudioSystem>  _audioSystem;
        unique_ptr<FrameRenderer> _frameRenderer;
        unique_ptr<RenderThread>  _renderThread;
        /** @brief GT 쪽 씬 스냅샷 빌더입니다. buildFromScene 의 재구축 판단 캐시가 프레임을 넘어 유지되도록 여기서 소유합니다. FrameRenderer 단계가 만들고 해제합니다
         *         (헤드리스 작업에는 없습니다).
         *         프레임마다 CPU 스냅샷만 exportCpuSnapshot 으로 뽑아 RenderFramePacket 에 담아 RT 로 넘깁니다.
         *         패킷과 함께 힙에 둡니다. 값으로 들면 이 헤더가 Graphics 의 씬 스냅샷 헤더들을 App 까지 끌고 갑니다(전방 선언으로 끊습니다). */
        unique_ptr<GpuSceneBuilder> _gpuSceneBuilder;
        /**
         * @brief GT 가 프레임마다 채우는 패킷입니다. 링의 자리와 바꿔 가며 돕니다(`RenderThread::submit`). FrameRenderer 단계가 만들고 해제합니다.
         * @details 링에서 돌아온 저장소를 그대로 다시 채웁니다(지역 변수로 두면 스냅샷의 배치 · 그룹 목록과 라이트 목록이
         *          프레임마다 새로 할당됩니다).
         */
        unique_ptr<RenderFramePacket> _packetScratch;
        Delegate<void()>              _onScenesReleased;
        /** @brief 기동 단계 `ModuleTypes` 가 부르는 호스트의 타입 공급자 로더입니다(`setModuleTypeLoader`). */
        ModuleTypeLoaderDelegate _moduleTypeLoader;
        /**
         * @brief 셰이더 라이브 리로드입니다. **Shipping 에는 없고**(파일째 빌드에서 빠집니다) Debug 에서만 실제로 만들어집니다.
         * @details 셰이더 파일을 지켜보다 다시 컴파일하는 **개발 도구**입니다. RHI 는 디바이스 추상화이지 파일 감시자가 있을 곳이
         *          아니므로 돌리는 쪽(EngineLoop)이 갖습니다.
         */
#if !defined( SW_SHIPPING )
        unique_ptr<ShaderRecompiler> _shaderRecompiler;
#endif
        /** @brief 에디터 Undo/Redo 전용이라 배포본에는 만들지 않습니다(목록의 HostCreated). */
        unique_ptr<CommandStack>   _commandStack;
        unique_ptr<GpuUploadQueue> _gpuUploadQueue;

        bool _bShellActionsBound;
        bool _bHeadless;
        bool _bHeadlessTaskFailed;

        /** @brief `-gv_profileFrames` 계측 한 회분입니다. 판정은 모두 이 안에 있고 루프는 두 줄만 부릅니다. */
        FrameProfileSession _profileSession;
        /** @brief 메모리 태그 예산 · 보고(배포본에서는 아무 일도 하지 않는다). */
        MemoryBudgetMonitor _memoryBudgetMonitor;
        /** @brief 기동 단계의 순서(`EngineInitStepList.xxx` 를 위상 정렬)와 초기화한 단계입니다. */
        EngineInitSequence _startup;
        /** @brief Config 단계가 읽은 엔진 설정입니다(`_configManager` 소유). Resource · EngineDefaultAssets · RHI 단계가 읽습니다. */
        const EngineConfig* _pEngineConfig;
    };
} // namespace sw
