/**
 * @file Engine/Foundation/EngineSubsystemRegistration.h
 * @brief 서비스 · 기동 단계 범위의 서브시스템이 **자기 .cpp 에서** 만드는 법을 등록하는 틀입니다(언리얼 `USubsystem` 이 모듈 안에서 스스로 등록되는 것과 같은 모양).
 *
 * @details 예전에는 `EngineSubsystemSet.cpp` 한 파일이 모든 서브시스템 헤더를 include 해 만들었다 — 그 헤더들의 인라인 함수를 이 TU 가 코드로 내어 가장 느린 TU 였다.
 *          이제 서브시스템마다 제 .cpp 에 `SW_ENGINE_SUBSYSTEM` 한 줄을 두고, 목록 함수(`addServiceSubsystems` · `addStartupSubsystems`)는 등록을 자리 순서대로 부르기만 한다.
 *
 *          **순서는 데이터가 아니라 이 파일의 자리 표(`SW_ENGINE_SUBSYSTEM_SLOTS`)가 정한다.** 정적 등록자가 달리는 순서(링크 순서)에 기대지 않는다 —
 *          자리마다 만드는 순서가 고정이고(범위의 소멸은 그 역순), 앞 자리가 만든 것을 뒷 자리 공장이 범위에서 찾아 쓴다(`SubsystemScope::get`).
 *          자리마다 등록이 정확히 하나여야 한다. 빠진 자리(링커가 오브젝트를 버렸거나 등록 줄을 지웠다) · 두 번 등록한 자리는 목록 함수가 오류로 멈춘다 — 폴백이 없다.
 *          클라이언트 전용 자리(렌더러 폴더 · 창)는 클라이언트 코드 빌드에서만 등록이 있어야 한다(Server 타깃은 그 폴더를 짓지 않는다).
 *
 *          이 헤더는 엔진 바닥 층(0)이다 — 위 층의 형식은 앞으로 선언만 한다. 공장이 쓰는 범위 · 호스트 값(`ServiceParts` · `StartupSetup` · `StartupParts`)도 여기 둔다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

namespace sw
{
    class CompressionCodecRegistry;
    class ConfigManager;
    class DebugKeyActions;
    class EngineDefaultAssetsSubsystem;
    class EventDispatcher;
    class FontService;
    class FrameRendererSubsystem;
    class GameNetSession;
    class IAudioService;
    class InputManager;
    class ReflectionSubsystem;
    class RenderThreadSubsystem;
    class RHISubsystem;
    class SceneManager;
    class TelemetryService;
    class UISystem;
    class UserSettingsManager;
    struct HeadlessTaskState;

    namespace engine
    {
        /** @brief 서비스 범위에서 호스트가 직접 쓰는 서브시스템입니다(범위가 소유한다). */
        struct ServiceParts
        {
            EventDispatcher* _pEventDispatcher{ nullptr };
            DebugKeyActions* _pDebugKeyActions{ nullptr }; ///< 셸 디버그 키 맵 — 엔진 종료가 입력 매니저보다 먼저 맵을 놓는다
        };

        /**
         * @brief 기동 단계 범위의 호스트별 값입니다.
         * @details `_reflection` · `_defaultAssets` · `_audio` 는 본문만 다른 대역을 넘기는 자리다(하네스). 비어 있으면 엔진의 클래스를 만든다.
         *          넘기는 쪽이 제 메모리 용도(`SW_MEMORY_SCOPE`) 아래에서 만든다. 형식은 앞으로 선언만 한다 — 값을 만들고 없애는 쪽(`EngineSubsystemSet.h` 를 include 하는
         *          호스트)과 그 자리를 비우는 공장(그 서브시스템의 .cpp)이 완전한 형식을 본다.
         */
        struct StartupSetup
        {
            HeadlessTaskState&                       _headlessState;
            bool                                     _bDedicatedServer{ SW_FALSE }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                                     _bRenderer{ false };           ///< 창 · RHI · 렌더러 · 렌더 스레드 · 씬 디바이스를 둔다(클라이언트 코드 빌드만) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            unique_ptr<ReflectionSubsystem>          _reflection{};
            unique_ptr<EngineDefaultAssetsSubsystem> _defaultAssets{};
            unique_ptr<IAudioService>                _audio{};
        };

        /** @brief 기동 단계 범위에서 호스트가 직접 쓰는 서브시스템입니다(범위가 소유한다). 렌더 쪽은 `_bRenderer` 가 꺼졌거나 서버 타깃이면 nullptr 입니다. */
        struct StartupParts
        {
            CompressionCodecRegistry* _pCodecRegistry{ nullptr };
            ConfigManager*            _pConfigManager{ nullptr };
            RHISubsystem*             _pRHI{ nullptr };
            FrameRendererSubsystem*   _pFrameRendererSubsystem{ nullptr };
            RenderThreadSubsystem*    _pRenderThread{ nullptr };
            InputManager*             _pInputManager{ nullptr };
            IAudioService*            _pAudio{ nullptr };
            SceneManager*             _pSceneManager{ nullptr };
            UserSettingsManager*      _pUserSettings{ nullptr };
            FontService*              _pFontService{ nullptr };
            TelemetryService*         _pTelemetry{ nullptr };
            UISystem*                 _pUISystem{ nullptr };
            GameNetSession*           _pNetSession{ nullptr };
        };

        /** @brief 공장 하나가 받는 값입니다. 서비스 범위 공장은 `_pServiceParts` 만, 기동 단계 공장은 `_pStartupSetup` · `_pStartupParts` 만 채워져 있다. */
        struct EngineSubsystemBuild
        {
            ServiceParts* _pServiceParts{ nullptr };
            StartupSetup* _pStartupSetup{ nullptr };
            StartupParts* _pStartupParts{ nullptr };
        };
    } // namespace engine
} // namespace sw

/**
 * @brief 자리 표입니다 — `X( 자리 이름, 범위, 클라이언트 전용 )`. 줄 순서가 만드는 순서다. 자리를 더하면 그 서브시스템의 .cpp 에 `SW_ENGINE_SUBSYSTEM( 자리 이름, ... )` 를 둔다.
 * @details 서비스 범위는 초기화 · 종료할 것이 없고 수명만 있는 공용 서비스다 — 엔진 범위의 어느 것보다 먼저 서고 나중에 사라진다. 기동 단계는 엔진이 쓰는 서브시스템이다.
 *          각 자리의 까닭(왜 그 자리인가)은 그 공장 옆 주석에 둔다.
 */
#define SW_ENGINE_SUBSYSTEM_SLOTS( X )                \
    X( SceneTransformStorage, Services, false )       \
    X( EventDispatcher, Services, false )             \
    X( LocalizationManager, Services, false )         \
    X( DebugOverlayState, Services, false )           \
    X( ComponentDefaults, Services, false )           \
    X( FrameProfiler, Services, false )               \
    X( ProfilerBackendSubsystem, Services, false )    \
    X( CrashReportService, Services, false )          \
    X( AssetLoadProfiler, Services, false )           \
    X( AssetDataKindRegistry, Services, false )       \
    X( TickBatchRegistry, Services, false )           \
    X( DevCommandRegistry, Services, false )          \
    X( DebugHUDRegistry, Services, false )            \
    X( ContentHashCache, Services, false )            \
    X( GameTimeScale, Services, false )               \
    X( QuitRequest, Services, false )                 \
    X( StateHashHistory, Services, false )            \
    X( DebugKeyActions, Services, false )             \
    X( PhysicsMeshShapeTable, Services, false )       \
    X( DebugDrawQueue, Services, false )              \
    X( VisualLogger, Services, false )                \
    X( RenderTargetRegistry, Services, false )        \
    X( GPUBreadcrumbLog, Services, false )            \
    X( RHIBackendRegistry, Services, false )          \
    X( RenderExtensionRegistry, Services, true )      \
    X( ScreenshotRequestQueue, Services, true )       \
    X( CompressionCodecRegistry, Startup, false )     \
    X( ReflectionSubsystem, Startup, false )          \
    X( TaskManager, Startup, false )                  \
    X( AsyncFileIO, Startup, false )                  \
    X( ModuleUnloadStageSubsystem, Startup, false )   \
    X( ModuleLoadStageSubsystem, Startup, false )     \
    X( MainWindowSubsystem, Startup, true )           \
    X( SplashSubsystem, Startup, true )               \
    X( RHISubsystem, Startup, true )                  \
    X( FrameRendererSubsystem, Startup, true )        \
    X( RenderThreadSubsystem, Startup, true )         \
    X( SceneRHISubsystem, Startup, true )             \
    X( InputManager, Startup, false )                 \
    X( ConfigManager, Startup, false )                \
    X( AssetManager, Startup, false )                 \
    X( TextureRenderTargetSink, Startup, false )      \
    X( EngineDefaultAssetsSubsystem, Startup, false ) \
    X( PackEngineSettingsSubsystem, Startup, false )  \
    X( Render2DSettingsSubsystem, Startup, false )    \
    X( AudioService, Startup, false )                 \
    X( AssetStreamingQueue, Startup, false )          \
    X( SceneManager, Startup, false )                 \
    X( HeadlessSubsystem, Startup, false )            \
    X( UserSettingsManager, Startup, false )          \
    X( ShaderCache, Startup, false )                  \
    X( PhysicsSystem, Startup, false )                \
    X( GameNetSession, Startup, false )               \
    X( FontService, Startup, false )                  \
    X( TelemetryService, Startup, false )             \
    X( UIClipboardService, Startup, false )           \
    X( UIGlobalCommandRegistry, Startup, false )      \
    X( UIBindingConverterRegistry, Startup, false )   \
    X( UIDocumentCache, Startup, false )              \
    X( UIStyleSheetCache, Startup, false )            \
    X( UIThemeController, Startup, false )            \
    X( UISystem, Startup, false )                     \
    X( UIAccessibilityService, Startup, false )       \
    X( UISubtitleService, Startup, false )            \
    X( UITooltipService, Startup, false )             \
    X( UINotificationService, Startup, false )        \
    X( UIOffscreenScreens, Startup, false )           \
    X( UIWidgetComponentService, Startup, false )

namespace sw::engine
{
    /** @brief 서브시스템 자리입니다. 값의 순서가 만드는 순서다(`SW_ENGINE_SUBSYSTEM_SLOTS`). */
    enum class EngineSubsystemSlot : uint16
    {
#define SW_ENGINE_SUBSYSTEM_SLOT_ENUM( Name, Stage, bClientOnly ) Name,
        SW_ENGINE_SUBSYSTEM_SLOTS( SW_ENGINE_SUBSYSTEM_SLOT_ENUM )
#undef SW_ENGINE_SUBSYSTEM_SLOT_ENUM
            Count
    };

    /** @brief 자리가 속한 범위입니다. */
    enum class EngineSubsystemStage : uint8
    {
        Services,
        Startup
    };

    /** @brief 공장입니다. 범위에 서브시스템을 넣고, 호스트가 직접 쓰는 것이면 `build` 의 범위 값에 적고, 안의 객체를 범위에 겁니다. */
    using EngineSubsystemFactory = void ( * )( SubsystemScope& scope, EngineSubsystemBuild& build );

    /**
     * @struct EngineSubsystemRegistrar
     * @brief `SW_ENGINE_SUBSYSTEM` 이 두는 정적 등록자입니다. 만들 때 엔진 이미지의 목록(`getHead`)에 달리기만 합니다 — 목록 함수가 자리 순서로 고쳐 부릅니다.
     * @details 엔진 이미지 안에서만 쓴다(개발은 DLL 이라 오브젝트가 모두 들어가고, 배포 정적 링크는 Engine 을 통째로 링크한다 — `sw_linkWholeArchive`).
     */
    struct SW_API EngineSubsystemRegistrar
    {
        EngineSubsystemSlot             _slot;
        EngineSubsystemFactory          _pfnFactory;
        const EngineSubsystemRegistrar* _pNext; ///< 목록의 다음(먼저 만든 쪽)

        static const EngineSubsystemRegistrar*& getHead();

        EngineSubsystemRegistrar( EngineSubsystemSlot slot, EngineSubsystemFactory pfnFactory );
        EngineSubsystemRegistrar( const EngineSubsystemRegistrar& )            = delete;
        EngineSubsystemRegistrar& operator=( const EngineSubsystemRegistrar& ) = delete;
    };

    /** @brief 자리의 이름(오류 문구)입니다. */
    SW_API const utf8* getEngineSubsystemSlotName( EngineSubsystemSlot slot );
    /** @brief 자리의 범위입니다. */
    SW_API EngineSubsystemStage getEngineSubsystemSlotStage( EngineSubsystemSlot slot );
    /** @brief 자리가 클라이언트 코드 빌드에만 있는지 나타냅니다(렌더러 폴더 · 창 — Server 타깃은 짓지 않는다). */
    SW_API bool isEngineSubsystemSlotClientOnly( EngineSubsystemSlot slot );

    /**
     * @brief @p stage 의 자리를 순서대로 부릅니다. 자리마다 등록이 정확히 하나여야 하고(이 빌드에 없는 클라이언트 전용 자리는 없어야 한다), 어긋나면 이유를 남기고 멈춥니다.
     * @details 목록 함수(`EngineSubsystemSet.cpp`)만 부른다.
     */
    SW_API void buildEngineSubsystems( EngineSubsystemStage stage, SubsystemScope& scope, EngineSubsystemBuild& build );
} // namespace sw::engine

/**
 * @brief 서브시스템을 만드는 법을 그 서브시스템의 .cpp 에 등록합니다. @p Function 은 `void( SubsystemScope&, EngineSubsystemBuild& )` 이다(포획 없는 람다 · 함수).
 * @code
 *     SW_ENGINE_SUBSYSTEM( FontService, []( SubsystemScope& scope, EngineSubsystemBuild& build ) { build._pStartupParts->_pFontService = &scope.emplaceSubsystem<FontService>(); } );
 * @endcode
 */
#define SW_ENGINE_SUBSYSTEM( Slot, Function )                                             \
    static const ::sw::engine::EngineSubsystemRegistrar s_engineSubsystemRegistrar_##Slot \
    {                                                                                     \
        ::sw::engine::EngineSubsystemSlot::Slot, Function                                 \
    }

/** @brief 인자 없이 만들고 호스트가 직접 쓰지 않는 서브시스템의 등록입니다(가장 흔한 모양). */
#define SW_ENGINE_SUBSYSTEM_DEFAULT( Slot, Type ) \
    SW_ENGINE_SUBSYSTEM( Slot, []( ::sw::SubsystemScope& scope, ::sw::engine::EngineSubsystemBuild& ) { (void)scope.emplaceSubsystem<Type>(); } )
