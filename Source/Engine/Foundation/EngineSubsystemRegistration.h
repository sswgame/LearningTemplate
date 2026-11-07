/**
 * @file Engine/Foundation/EngineSubsystemRegistration.h
 * @brief 엔진 서브시스템과 서비스 칸을 **그 객체를 범위에 넣는 .cpp 에서** 등록하는 틀입니다 — 중앙 목록이 없습니다.
 *
 * @details 언리얼 `UEngineSubsystem` 이 모듈 안에서 스스로 등록되고 `GEngine->GetEngineSubsystem<T>()` 로 타입으로 찾히는 것과 같은 모양입니다.
 *          등록 줄 하나가 그 타입의 모든 것을 말합니다.
 *          | 줄 | 하는 일 |
 *          | --- | --- |
 *          | `SW_ENGINE_SERVICE_SUBSYSTEM( Type, 노출, 단계, ( 먼저 만들 타입… ), 공장 )` | 서브시스템을 만들고 서비스 칸에 묶는다(가장 흔한 모양) |
 *          | `SW_ENGINE_SUBSYSTEM( Type, 단계, ( 먼저 만들 타입… ), 공장 )` | 서브시스템을 만들기만 한다(서비스 칸이 없다 — 범위로만 찾는다) |
 *          | `SW_ENGINE_SERVICE( Type, 노출 )` | 다른 서브시스템이 범위에 건 객체(값 묶음 · 안의 객체)를 서비스 칸에 묶는다 |
 *          - **필수 여부 · const** 는 타입 헤더의 칸 선언(`SW_SERVICE_SLOT`, `Core/Subsystem/ServiceSlot.h`)이 말한다 — 부르는 TU 가 컴파일 때 알아야 한다.
 *          - **노출**: `GameVisible` 이면 게임 모듈(SWGame)의 범위에도 건다(`engine::bindModuleServices`), `HostOnly` 면 호스트 · 에디터 전용.
 *          - **단계**: `Services`(초기화 · 종료할 것이 없고 수명만 있는 공용 서비스 — 엔진 범위의 어느 것보다 먼저 서고 나중에 사라진다) · `Startup`(기동 단계).
 *          - **만드는 순서**: 공장이 범위에서 다른 서브시스템을 찾으면(`scope.get<T>()`) 그 타입을 괄호 안에 적는다(언리얼 `Collection.InitializeDependency<T>()`).
 *            같은 단계 안에서 그 순서로 위상 정렬하고, 동점은 이름 순이다(링크 순서에 기대지 않는다). 모르는 타입 · 다른 단계 · 순환은 기동 오류다.
 *            초기화 · 종료 · 소멸 순서는 이것이 아니라 서브시스템이 선언한 의존(`ISubsystem::getSubsystemDependencies`)이 정한다 — 이 순서는 공장의 찾기만 맞춘다.
 *          서비스를 더하려면 헤더에 칸 선언 한 줄, 범위에 넣는 .cpp 에 등록 한 줄이다. 줄이 빠지면 그 칸을 읽는 쪽이 링크 오류이고, 필수 칸이 비면 엔진이
 *          그 이름을 대며 서지 않는다(`engine::bindEngineServices`). 클라이언트 전용 폴더(Renderer)는 Server 타깃이 짓지 않으므로 그 등록도 없다.
 *
 *          이 헤더는 엔진 바닥 층(0)이다 — 위 층의 형식은 앞으로 선언만 한다. 공장이 쓰는 범위 · 호스트 값(`ServiceParts` · `StartupSetup` · `StartupParts`)도 여기 둔다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ServiceSlot.h"
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
    class IEngineRenderDriver;
    class InputManager;
    class ReflectionSubsystem;
    class RenderThreadSubsystem;
    class RHISubsystem;
    class SceneManager;
    class TelemetryService;
    class UISystem;
    class IEngineUIDriver;
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
            bool                                     _bRenderer{ false };           ///< 창 · RHI · 렌더러 · 렌더 스레드 · 씬 디바이스를 둔다(전용 서버 역할은 끈다 — `Engine::initialize`) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
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
            IEngineRenderDriver*      _pRenderDriver{ nullptr }; ///< 엔진 루프의 렌더러 창구(`EngineRenderDriver`)
            FrameRendererSubsystem*   _pFrameRendererSubsystem{ nullptr };
            RenderThreadSubsystem*    _pRenderThread{ nullptr };
            InputManager*             _pInputManager{ nullptr };
            IAudioService*            _pAudio{ nullptr };
            SceneManager*             _pSceneManager{ nullptr };
            UserSettingsManager*      _pUserSettings{ nullptr };
            FontService*              _pFontService{ nullptr };
            TelemetryService*         _pTelemetry{ nullptr };
            UISystem*                 _pUISystem{ nullptr };
            IEngineUIDriver*          _pUIDriver{ nullptr }; ///< 엔진 루프의 UI 창구(`UISystem`). 서버는 UI 를 짓지 않아 비어 있다
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

namespace sw::engine
{
    /** @brief 서비스 칸을 게임 모듈에도 보이는지입니다. */
    enum class EngineServiceVisibility : uint8
    {
        GameVisible, ///< 게임 모듈(SWGame)의 범위에도 건다
        HostOnly,    ///< 호스트 · 에디터 전용 — 게임 모듈은 찾지 못한다
    };

    /** @brief 서브시스템을 만드는 범위(단계)입니다. 단계 사이의 순서는 호스트가 정한다(서비스 범위가 먼저 선다). */
    enum class EngineSubsystemStage : uint8
    {
        Services,
        Startup,
    };

    /** @brief 공장입니다. 범위에 서브시스템을 넣고, 호스트가 직접 쓰는 것이면 `build` 의 범위 값에 적고, 안의 객체를 범위에 겁니다. */
    using EngineSubsystemFactory = void ( * )( SubsystemScope& scope, EngineSubsystemBuild& build );

    class EngineRegistrationList;

    /**
     * @struct EngineServiceRecord
     * @brief 서비스 칸 하나의 등록입니다(`SW_ENGINE_SERVICE*` 가 정적으로 둔다). 만들 때 목록에 달리기만 한다.
     */
    struct SW_API EngineServiceRecord
    {
        const utf8*  _pTypeName;                            ///< 오류 문구 · 정렬에 쓰는 타입 이름
        uint64       _typeHash;                             ///< 범위에서 찾는 타입 해시(`getSubsystemTypeHash`)
        ServiceSlot* _pSlot;                                ///< 그 타입의 칸(`ServiceSlotOf<T>::s_slot`)
        bool ( *_pfnBind )( SubsystemScope& scope, void* ); ///< 모듈 범위에 그 타입으로 건다(`SubsystemScope::bind<T>`)
        ServiceRequirement         _requirement;            ///< 헤더의 칸 선언이 정한 필수 여부
        EngineServiceVisibility    _visibility;             ///< 게임 모듈 노출
        const EngineServiceRecord* _pNext;                  ///< 목록의 다음(먼저 단 쪽)

        EngineServiceRecord( EngineRegistrationList& list, const utf8* pTypeName, uint64 typeHash, ServiceSlot& slot, bool ( *pfnBind )( SubsystemScope&, void* ),
                             ServiceRequirement requirement, EngineServiceVisibility visibility );
        EngineServiceRecord( const EngineServiceRecord& )            = delete;
        EngineServiceRecord& operator=( const EngineServiceRecord& ) = delete;
    };

    /**
     * @struct EngineSubsystemRecord
     * @brief 서브시스템 공장 하나의 등록입니다(`SW_ENGINE_SUBSYSTEM` · `SW_ENGINE_SERVICE_SUBSYSTEM` 이 정적으로 둔다).
     */
    struct SW_API EngineSubsystemRecord
    {
        const utf8*                  _pTypeName;     ///< 오류 문구 · 동점 순서에 쓰는 타입 이름
        uint64                       _typeHash;      ///< 의존이 가리키는 타입 해시
        EngineSubsystemStage         _stage;         ///< 만드는 단계
        span<const uint64>           _listAfterHash; ///< 먼저 만들 타입의 해시(같은 단계)
        const utf8*                  _pAfterNames;   ///< 먼저 만들 타입들의 글(`( A, B )`, 오류 문구용)
        EngineSubsystemFactory       _pfnFactory;    ///< 만드는 법
        const EngineSubsystemRecord* _pNext;         ///< 목록의 다음(먼저 단 쪽)

        EngineSubsystemRecord( EngineRegistrationList& list, const utf8* pTypeName, uint64 typeHash, EngineSubsystemStage stage, span<const uint64> listAfterHash,
                               const utf8* pAfterNames, EngineSubsystemFactory pfnFactory );
        EngineSubsystemRecord( const EngineSubsystemRecord& )            = delete;
        EngineSubsystemRecord& operator=( const EngineSubsystemRecord& ) = delete;
    };

    /**
     * @class EngineRegistrationList
     * @brief 등록 목록입니다. 엔진은 이미지 하나의 전역 목록(`getGlobal`)만 쓰고, 테스트는 자기 목록에 자기 등록을 달아 같은 규칙을 시험한다.
     * @details 엔진 이미지 안에서만 등록한다(개발은 DLL 이라 오브젝트가 모두 들어가고, 배포 정적 링크는 Engine 을 통째로 링크한다 — `sw_linkWholeArchive`).
     */
    class SW_API EngineRegistrationList
    {
    public:
        EngineRegistrationList();
        EngineRegistrationList( const EngineRegistrationList& )            = delete;
        EngineRegistrationList& operator=( const EngineRegistrationList& ) = delete;

        /** @brief 엔진 이미지의 목록입니다(등록 매크로가 단다). 처음 부를 때 만든다 — 정적 등록자의 초기화 순서에 기대지 않는다. */
        static EngineRegistrationList& getGlobal();

        /**
         * @brief @p subsystems(부모 범위까지)에서 칸마다 그 타입을 한 번 찾아 둡니다.
         * @return 비어 있는 필수 칸의 타입 이름(이름 순으로 첫 것)입니다. 모두 찾았으면 nullptr 입니다.
         */
        [[nodiscard]] const utf8* bindServices( const ISubsystemLocator& subsystems );
        /** @brief 칸을 모두 비웁니다. */
        void unbindServices();
        /** @brief 지금 칸에서 비어 있는 필수 칸의 타입 이름(이름 순으로 첫 것)입니다. 모두 있으면 nullptr 입니다. */
        [[nodiscard]] const utf8* findMissingRequiredService() const;
        /**
         * @brief 지금 칸의 객체를 @p outScope 에 그 타입으로 빌려 겁니다. 빈 칸은 걸지 않는다.
         * @param bGameModuleOnly true 면 `GameVisible` 칸만 건다.
         * @param bSkipAlreadyBound true 면 @p outScope 에 이미 걸린 타입은 건너뛴다. false 면 겹침은 결함이라 멈춘다.
         */
        void bindModuleServices( SubsystemScope& outScope, bool bGameModuleOnly, bool bSkipAlreadyBound ) const;

        /**
         * @brief @p stage 의 공장을 만드는 순서로 늘어놓습니다(선언한 먼저 만들 타입으로 위상 정렬, 동점은 이름 순).
         * @return 같은 타입 둘 · 모르는 타입 · 다른 단계의 타입 · 순환이면 false 이고 @p outError 에 무엇인지 적습니다.
         */
        [[nodiscard]] bool orderSubsystems( EngineSubsystemStage stage, vector<const EngineSubsystemRecord*>& outListOrder, string& outError ) const;
        /** @brief @p stage 의 공장을 만드는 순서로 부릅니다. 순서를 정하지 못하면 이유를 남기고 멈춘다 — 폴백이 없다. */
        void buildSubsystems( EngineSubsystemStage stage, SubsystemScope& scope, EngineSubsystemBuild& build ) const;

    private:
        friend struct EngineServiceRecord;
        friend struct EngineSubsystemRecord;

        const EngineServiceRecord*   _pServiceHead;   ///< 서비스 칸 등록(나중에 단 것이 앞). 칸은 등록이 가리키는 곳에 있어 등록은 읽기만 한다
        const EngineSubsystemRecord* _pSubsystemHead; ///< 공장 등록(나중에 단 것이 앞)
    };

    /** @brief 등록 줄이 쓰는 타입별 함수입니다. */
    template <class T>
    struct EngineRegistrationOps
    {
        /** @brief @p pObject 를 @p scope 에 타입 @p T 로 겁니다. */
        static bool bind( SubsystemScope& scope, void* pObject ) { return scope.bind( static_cast<T*>( pObject ) ); }
        /** @brief 인자 없이 만들어 범위에 넣습니다(호스트가 직접 쓰지 않는 서브시스템 — 가장 흔한 공장). */
        static void emplace( SubsystemScope& scope, EngineSubsystemBuild& build )
        {
            (void)build;
            (void)scope.emplaceSubsystem<T>();
        }
    };

    /** @brief 먼저 만들 타입들의 해시입니다. 타입 이름이라 철자가 틀리면 컴파일 오류다(선언만 있으면 된다). */
    template <class... TAfter>
    struct EngineSubsystemAfter
    {
        /// @brief 해시 배열입니다. 끝의 0 은 빈 묶음도 배열이게 하는 자리다. 등록은 주소만 들고 값은 기동(정적 초기화가 끝난 뒤)에 읽는다 — 초기화 순서에 기대지 않는다.
        static inline const uint64 kArrHash[sizeof...( TAfter ) + 1] = { getSubsystemTypeHash<TAfter>()..., 0 };
        static span<const uint64>  getHashes() { return span<const uint64>{ kArrHash, sizeof...( TAfter ) }; }
    };
} // namespace sw::engine

// 인자가 **타입 이름**이라 괄호를 씌울 수 없다(`ServiceSlotOf<(Type)>` 는 올바른 문법이 아니다).
// NOLINTBEGIN(bugprone-macro-parentheses)
#define SW_ENGINE_REGISTRATION_UNPAREN( ... ) __VA_ARGS__

/** @brief 서비스 칸을 정의하고 목록에 답니다. namespace `sw` 안에 둔다. */
#define SW_ENGINE_SERVICE( Type, visibility )                                                                                                                                           \
    ServiceSlot                                    ServiceSlotOf<Type>::s_slot{ nullptr }; /* 정의는 namespace `sw` 안이라야 한다(`::sw::ServiceSlot ::sw::…` 는 한 이름으로 읽힌다) */ \
    static const ::sw::engine::EngineServiceRecord s_engineServiceRecord_##Type                                                                                                         \
    {                                                                                                                                                                                   \
        ::sw::engine::EngineRegistrationList::getGlobal(), ServiceSlotOf<Type>::kTypeName, ::sw::getSubsystemTypeHash<Type>(), ServiceSlotOf<Type>::s_slot,                             \
            &::sw::engine::EngineRegistrationOps<Type>::bind, ServiceSlotOf<Type>::kRequirement, ::sw::engine::EngineServiceVisibility::visibility                                      \
    }

/**
 * @brief 서브시스템을 만드는 법을 목록에 답니다. @p after 는 괄호로 싼 타입 목록(`()` · `( TaskManager )`)이고, @p Function 은 `void( SubsystemScope&, EngineSubsystemBuild& )` 다.
 * @code
 *     SW_ENGINE_SUBSYSTEM( SceneRHISubsystem, Startup, ( RHISubsystem ), []( SubsystemScope& scope, engine::EngineSubsystemBuild& build ) { ... } );
 * @endcode
 */
#define SW_ENGINE_SUBSYSTEM( Type, stage, after, Function )                                                                                      \
    static const ::sw::engine::EngineSubsystemRecord s_engineSubsystemRecord_##Type                                                              \
    {                                                                                                                                            \
        ::sw::engine::EngineRegistrationList::getGlobal(), #Type, ::sw::getSubsystemTypeHash<Type>(), ::sw::engine::EngineSubsystemStage::stage, \
            ::sw::engine::EngineSubsystemAfter<SW_ENGINE_REGISTRATION_UNPAREN after>::getHashes(), #after, Function                              \
    }

/** @brief 서브시스템을 만들고 그 타입의 서비스 칸에 묶습니다(위 둘을 한 줄로). @p Function 이 넣은 것(또는 건 객체)이 @p Type 으로 찾혀야 한다. */
#define SW_ENGINE_SERVICE_SUBSYSTEM( Type, visibility, stage, after, Function ) \
    SW_ENGINE_SERVICE( Type, visibility );                                      \
    SW_ENGINE_SUBSYSTEM( Type, stage, after, Function )

/** @brief 인자 없이 만들고 호스트가 직접 쓰지 않는 공장입니다(`SW_ENGINE_SUBSYSTEM` 의 @p Function 자리). */
#define SW_ENGINE_DEFAULT_FACTORY( Type ) &::sw::engine::EngineRegistrationOps<Type>::emplace
// NOLINTEND(bugprone-macro-parentheses)
