/**
 * @file EngineLoop.h
 * @brief 엔진 코어 메인 루프 및 서브시스템 소유권 관리
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/Common.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"

namespace sw
{
    struct DebugOverlayState;
    struct EngineData;

    class ActionMap;
    class AssetStreamingQueue;
    class CameraComponent;
    class CommandLineManager;
    class CommandStack;
    class ComponentDefaults;
    class CompressionCodecRegistry;
    class ConfigManager;
    class DeadlockDetector;
    class DebugDrawQueue;
    class EventDispatcher;
    class FrameDoubleBuffer;
    class FrameRenderer;
    class GlobalVariableManager;
    class IAudioSystem;
    class InputManager;
    class IRHIDevice;
    class LiveReloadManager;
    class LocalizationManager;
    class Logger;
    class MemoryProfiler;
    class ReloadFileManager;
    class RenderThread;
    class ResourceManager;
    class RHI;
    class RHIBackendRegistry;
    class SceneManager;
    class ShaderCache;
    class TaskManager;
    class TypeRegistry;

    /**
     * @brief 이번 프레임 뷰 카메라를 돌려줍니다.
     * @details tick 내부의 핫리로드/씬 전환이 GameObject 를 파괴할 수 있으므로,
     *          카메라는 미리 캡처하지 않고 파괴 단계가 끝난 뒤 이 델리게이트로 조회합니다.
     */
    SW_DECLARE_DELEGATE( CameraComponent*, ViewCameraProviderDelegate, void );

    /**
     * @class EngineLoop
     * @brief App이 보유하던 코어 매니저들을 캡슐화하고 메인 루프(tick)를 담당합니다.
     */
    class SW_API EngineLoop
    {
    public:
        EngineLoop();
        ~EngineLoop();

        EngineLoop( const EngineLoop& )            = delete;
        EngineLoop& operator=( const EngineLoop& ) = delete;

        /** @brief 서브시스템(윈도우, RHI 포함)을 초기화합니다. */
        bool initialize( int32 argc, utf8* pArgv[] );
        /** @brief 매니저들을 종료하고 정리합니다. */
        void shutdown();

        /** @brief 입력 등을 시작하는 프레임의 첫 단계입니다. */
        void beginFrame();
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
        bool applyPendingBackendChange();

        // ----------------------------------------------------------------------
        // 헬퍼
        // ----------------------------------------------------------------------
        void setPresentHook( sw::PresentHookDelegate presentHook );
        void setPostPresentHook( sw::PresentHookDelegate postPresentHook );
        void updateShellActions( float32 deltaTime );

        /**
         * @brief 엔진이 스스로 종료를 원하면 true (`-ProfileFrames N` 을 다 채운 경우).
         * @details 창 수명은 App 이 쥐고 있으므로 여기서는 의사만 알린다.
         */
        bool wantsQuit() const { return _bWantsQuit != 0; }

        /** @brief `-ProfileFrames N` 의 N. 0 이면 계측하지 않습니다. */
        uint64 _profileFrameTarget{ 0 };
        /** @brief 보고를 이미 냈으면 true — 매 프레임 다시 찍지 않습니다. */
        uint8 _bProfileReported{ 0 };
        /** @brief 프로파일 목표 프레임을 채워 종료하려 하면 true. */
        uint8 _bWantsQuit{ 0 };
        /** @brief 워밍업 구간을 이미 버렸으면 true. */
        uint8 _bProfileWarmedUp{ 0 };
        void  pollDebugHotkeys( const Delegate<void( const utf8* )>& forceReloadCallback );
        /** @brief 셸 디버그 ActionMap에서 해당 액션이 이번 프레임 발동했는지 반환합니다. */
        bool wasDebugActionTriggered( string_view actionName ) const;

        // ----------------------------------------------------------------------
        // Getter (App이 ModuleHost 등과 연동하기 위해 필요)
        // ----------------------------------------------------------------------
        LiveReloadManager*        getLiveReloadManager() const { return _liveReloadManager.get(); }
        ConfigManager*            getConfigManager() const { return _configManager.get(); }
        CommandLineManager*       getCommandLineManager() const { return _commandLineManager.get(); }
        LocalizationManager*      getLocalizationManager() const { return _localizationManager.get(); }
        RHI*                      getRHI() const { return _rhi.get(); }
        RenderThread*             getRenderThread() const { return _renderThread.get(); }
        CompressionCodecRegistry* getCompressionCodecRegistry() const { return _compressionCodecRegistry.get(); }
        ShaderCache*              getShaderCache() const { return _shaderCache.get(); }
        ComponentDefaults*        getComponentDefaults() const { return _componentDefaults.get(); }
        bool                      isHeadless() const { return _bHeadless; }

    private:
        /** @brief 디바이스 재생성 후 FrameRenderer·RenderThread·Scene을 다시 붙입니다. */
        void rebindSceneAfterDeviceRecreate();

    private:
        unique_ptr<Logger>                _logger;
        unique_ptr<DeadlockDetector>      _deadlockDetector;
        unique_ptr<MemoryProfiler>        _memoryProfiler;
        unique_ptr<CommandLineManager>    _commandLineManager;
        unique_ptr<TaskManager>           _taskManager;
        unique_ptr<GlobalVariableManager> _globalVariableManager;
        unique_ptr<TypeRegistry>          _typeRegistry;
        unique_ptr<ConfigManager>         _configManager;
        unique_ptr<LocalizationManager>   _localizationManager;
        unique_ptr<ResourceManager>       _resourceManager;
        unique_ptr<RHI>                   _rhi;
        unique_ptr<LiveReloadManager>     _liveReloadManager;
        unique_ptr<ReloadFileManager>     _reloadFileManager;
        unique_ptr<SceneManager>          _sceneManager;
        unique_ptr<InputManager>          _inputManager;
        unique_ptr<ActionMap>             _mapDebugAction;
        unique_ptr<IAudioSystem>          _audioSystem;
        unique_ptr<EventDispatcher>       _eventDispatcher;
        unique_ptr<FrameRenderer>         _frameRenderer;
        unique_ptr<RenderThread>          _renderThread;
        /** @brief GT 쪽 영속 GpuScene — buildFromScene의 콘텐츠 해시 캐싱이 프레임 간 유지되도록 여기 소유.
         *         매 프레임 CPU 스냅샷만 exportCpuSnapshot으로 뽑아 RenderFramePacket에 담아 RT로 넘긴다. */
        GpuScene                             _gtGpuScene;
        unique_ptr<EngineData>               _engineData;
        unique_ptr<AssetStreamingQueue>      _assetStreamingQueue;
        unique_ptr<CommandStack>             _commandStack;
        unique_ptr<DebugOverlayState>        _debugOverlayState;
        unique_ptr<DebugDrawQueue>           _debugDrawQueue;
        unique_ptr<FrameDoubleBuffer>        _frameDoubleBuffer;
        unique_ptr<RHIBackendRegistry>       _rhiBackendRegistry;
        unique_ptr<CompressionCodecRegistry> _compressionCodecRegistry;
        unique_ptr<ShaderCache>              _shaderCache;
        unique_ptr<ComponentDefaults>        _componentDefaults;

        bool _bShellActionsBound;
        bool _bHeadless;
    };
} // namespace sw
