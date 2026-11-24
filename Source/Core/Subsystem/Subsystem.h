/**
 * @file Core/Subsystem/Subsystem.h
 * @brief 서브시스템의 계약입니다. 스스로 이름 · 의존 · 도는 호스트를 선언하고 초기화 · 종료를 구현합니다.
 * @details 언리얼 `USubsystem`(`Initialize` · `Deinitialize` · `InitializeDependency` · `ShouldCreateSubsystem`)과 같은 자리입니다. 순서는 손으로 적지 않습니다 —
 *          `SubsystemCollection` 이 선언된 의존으로 위상 정렬해(동점은 이름 순) 초기화하고, 역순으로 멈춘(stop) 뒤 역순으로 종료하고, 소멸도 그 역순입니다.
 *          초기화 · 종료 짝과 의존 순서는 엔진에 매인 개념이 아니라 Core 에 둔다 — Core 의 매니저(태스크 · 비동기 IO · 코덱 레지스트리)도 직접 상속한다.
 *          어떤 서브시스템이 있고 호스트 비트가 무엇을 뜻하는지(클라이언트 · 전용 서버)는 쓰는 쪽(엔진)이 정한다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/MemoryTag.h"

#include <type_traits>

namespace sw
{
    /**
     * @brief 서브시스템이 도는 호스트 종류의 비트 마스크입니다. 비트의 뜻은 쓰는 쪽이 정한다(엔진은 `EngineSubsystemHost`).
     * @details `All` 은 모든 비트라, 호스트 종류를 늘려도 "어디서나 도는" 선언은 고치지 않는다.
     */
    enum class SubsystemHostMask : uint8
    {
        None = 0,
        All  = 0xFF,
    };

    /** @brief 두 마스크가 한 비트라도 겹치면 true 입니다(이 호스트에서 도는가). */
    constexpr bool overlapsHostMask( SubsystemHostMask lhs, SubsystemHostMask rhs )
    {
        return ( static_cast<uint8>( lhs ) & static_cast<uint8>( rhs ) ) != 0;
    }
} // namespace sw

namespace sw
{
    /** @brief 서브시스템 하나의 초기화 결과입니다. */
    enum class SubsystemInitResult : uint8
    {
        Succeeded,      ///< 다음 서브시스템으로 갑니다
        Failed,         ///< 기동을 멈춥니다. 이미 초기화한 서브시스템은 `shutdownAll` 이 역순으로 내립니다
        SkipDependents, ///< 이 서브시스템은 초기화됐지만, 이것에 (간접으로라도) 의존하는 서브시스템은 돌지 않습니다(헤드리스 작업)
    };

    /**
     * @brief 서브시스템이 든 상태의 수명입니다. 등록 줄(`SW_ENGINE_SUBSYSTEM` · `SW_MODULE_SUBSYSTEM` …)이 **꼭 고른다** — 기본값이 없다.
     * @details 예전에는 엔진 서비스가 모두 프로세스 수명이라 떠 있는 자막 · 알림 · 재생 중인 소리 · 열린 화면이 게임을 다시 시작해도 남았고, 샐 때마다 하나씩 자동화 되돌리기로 등록했다.
     *          언리얼이 `UEngineSubsystem` 과 `UGameInstanceSubsystem` 을 형식으로 가르는 자리다. 여기서는 객체를 다시 만들지 않고 상태만 비운다 —
     *          서비스를 빌려 든 쪽(칸 · 다른 서브시스템)이 많고, 장치 · 구독처럼 게임을 넘어 남을 것과 한 객체에 있다.
     *          `GameInstance` 는 `Subsystem` 의 두 함수를 **재정의해야 하고** `Process` 는 재정의하면 안 된다(`kSubsystemLifetimeMatches` — 컴파일 오류).
     */
    enum class SubsystemLifetime : uint8
    {
        Process,      ///< 게임을 다시 시작해도 남는다 — 종류 표 · 캐시 · 장치 · 주인이 스스로 빼는 등록 · 사용자 설정 · 도구 설정
        GameInstance, ///< 게임 인스턴스와 함께 사라진다 — 떠 있는 것 · 기다리는 것 · 쌓인 기록
    };

    /** @brief 게임 인스턴스의 수명 경계입니다(`Subsystem::resetGameInstanceState`). */
    enum class GameInstanceBoundary : uint8
    {
        Ended,    ///< 인스턴스가 끝났다(게임 다시 시작 · 타이틀 복귀 · PIE 멈춤) — 떠 있는 것을 비운다
        Starting, ///< 다음 인스턴스가 시작한다(게임 다시 시작 · PIE 시작) — 끝난 뒤에도 읽는 기록(비주얼 로그 · 추적)은 여기서 비운다(언리얼 Visual Logger 와 같다)
    };

    /**
     * @brief 서브시스템이 제 범위 밖의 **모듈 서비스 범위**에 보이는지입니다. 형식 상수 `kModuleVisibility` 로 서브시스템이 스스로 선언한다(`SubsystemScope::addSubsystem` 이 칸에 옮긴다).
     * @details 호스트가 모듈에 넘기는 범위가 손 목록으로 빌려 걸던 자리다 — 목록에서 빠지면 모듈이 조용히 못 찾았다. 이 길로는 게임 모듈에 닿을 수 없다:
     *          게임 모듈에 보이는 것은 엔진 서비스 칸(`GameVisible`)과 모듈이 공개한 서브시스템(`SW_MODULE_SUBSYSTEM`)뿐이다 — 호스트 전용의 경계가 형식으로 선다.
     *          언리얼 `UEditorSubsystem`(에디터만 찾는 서브시스템)과 같은 자리다. 서브시스템이 드러낸 멤버(`SubsystemScope::exposeMember`)도 그 멤버의 형식이 같은 상수로 선언한다
     *          (`kModuleVisibilityOf`) — 파일 감시 창구(`FileWatchService`)가 드러낸 칸에 선언이 닿지 않아 에디터가 조용히 못 찾았다(에셋 핫 리로드 전체가 꺼졌다).
     */
    enum class SubsystemModuleVisibility : uint8
    {
        ScopeOnly,   ///< 제 범위(와 그 아래 범위)의 조회에만 보인다(기본)
        HostModules, ///< 호스트 모듈(에디터)의 서비스 범위에도 보인다 — 게임 모듈은 찾지 못한다(`ModuleRegistrationHost::getHostModuleLocator`)
    };

    /** @brief 형식 @p T 가 선언한 노출입니다(`T::kModuleVisibility`). 선언하지 않았으면 `ScopeOnly` 다. */
    template <typename T, typename = void>
    inline constexpr SubsystemModuleVisibility kModuleVisibilityOf = SubsystemModuleVisibility::ScopeOnly;
    template <typename T>
    inline constexpr SubsystemModuleVisibility kModuleVisibilityOf<T, std::void_t<decltype( T::kModuleVisibility )>> = T::kModuleVisibility;
} // namespace sw

namespace sw
{
    /**
     * @class Subsystem
     * @brief 서브시스템입니다. `SubsystemCollection` 이 소유하고 의존 순서로 초기화 · 종료 · 소멸합니다.
     * @details - 이름은 다른 서브시스템의 의존 칸이 부르는 식별자입니다. 문자열 리터럴(프로세스 수명)을 돌려줍니다. 엔진 서브시스템은 클래스의
     *            `kSubsystemName` 상수로 두고 의존 칸은 그 상수를 가리킨다 — 철자가 틀리면 컴파일 오류다.
     *          - 의존은 먼저 서야 하는 서브시스템 이름들입니다. 모르는 이름 · 순환 · 호스트 어긋남은 정렬 오류입니다.
     *          - `initializeSubsystem` 은 백엔드 교체(`shutdownDependents` → `restartStopped`)로 다시 불릴 수 있습니다 — 이미 있는 것은 다시 씁니다.
     *          - 종료는 두 번 역순으로 돕니다. 먼저 `stopSubsystem`(종료 전 단계 — 새 일 받기를 멈추고 남은 일을 비운다)이 모두에 불리고,
     *            그다음 `shutdownSubsystem` 이 모두에 불립니다(언리얼의 PreExit 뒤 `Deinitialize`). 둘 다 **초기화에 성공한** 서브시스템에만,
     *            성공한 초기화 한 번마다 한 번씩 불립니다. 이때 다른 서브시스템은 아직 모두 살아 있습니다.
     *          - 소멸자는 모든 종료 뒤에 **모든** 서브시스템에(초기화하지 않았어도) 역순으로 불립니다.
     *          클래스가 따로 가진 `initialize( … )` · `shutdown()`(테스트가 자기 인스턴스로 쓰는 것)과 이름을 나눠, 컬렉션이 부르는 자리만 이 둘입니다.
     */
    class Subsystem
    {
    public:
        Subsystem()                              = default;
        virtual ~Subsystem()                     = default;
        Subsystem( const Subsystem& )            = delete;
        Subsystem& operator=( const Subsystem& ) = delete;

        /** @brief 서브시스템을 세웁니다. 위상 순서로 불리고, 그동안 `getSubsystemMemoryTag` 가 걸립니다. */
        virtual SubsystemInitResult initializeSubsystem() = 0;
        /**
         * @brief 종료 전 단계입니다 — 새 일 받기를 멈추고 남은 일을 비웁니다. 어느 `shutdownSubsystem` 보다도 먼저, 초기화에 성공한 것만 역순으로 불립니다.
         * @details 다른 서브시스템의 남은 일(완료 콜백)이 이 서브시스템을 부를 수 있으면 그 일을 여기서 비우고, 객체를 놓는 것은 `shutdownSubsystem` 에 둡니다.
         */
        virtual void stopSubsystem() {}
        /** @brief 세운 서브시스템을 내립니다. 모든 `stopSubsystem` 뒤에, 초기화에 성공한 것만 역순으로 불립니다. */
        virtual void shutdownSubsystem() = 0;

        /**
         * @brief 게임 인스턴스 수명의 상태를 비웁니다(`SubsystemLifetime::GameInstance` 만 재정의한다). 게임 스레드에서 초기화의 역순으로 불립니다.
         * @details 상태만 비운다 — 구독 · 등록은 건 쪽이 푼다. 타이틀 복귀 · PIE 에서는 게임 인스턴스와 씬이 아직 살아 있으므로, 남이 쥔 핸들은 세대로 무효가 되어야 한다.
         */
        virtual void resetGameInstanceState( GameInstanceBoundary boundary ) { (void)boundary; }
        /** @brief 지금 든 게임 인스턴스 상태의 이름입니다(`"queued subtitles"`). 비었으면 nullptr 입니다. `resetGameInstanceState` 가 비우는 것과 같은 것을 본다(개발의 누수 검사가 이름을 댄다). */
        virtual const utf8* findGameInstanceState() const { return nullptr; }

        /** @brief 이름입니다(의존 칸이 부르는 식별자, 문자열 리터럴). */
        virtual const utf8* getSubsystemName() const = 0;
        /** @brief 먼저 서야 하는 서브시스템 이름들입니다(없으면 빈 구간). 배열은 프로세스 수명이어야 합니다(`static constexpr`). */
        virtual span<const utf8* const> getSubsystemDependencies() const { return {}; }
        /**
         * @brief 이 서브시스템 **뒤에** 서야 하는 서브시스템 이름들입니다(그들이 이것에 의존한다 — 역방향 의존). 목록에 없는 이름은 건너뜁니다.
         * @details 이름 상수는 층을 따라 아래로만 include 된다. 그래서 낮은 층의 서브시스템(비동기 IO · 오디오 · 입력)이 높은 층의 것(모듈 라이브러리)보다
         *          늦게 서야 하면, 높은 층 쪽이 이 칸에 낮은 층의 이름을 적는다. 그 호스트에 없는 이름은 순서에 영향이 없다(선택 의존).
         */
        virtual span<const utf8* const> getSubsystemDependents() const { return {}; }
        /** @brief 이 서브시스템이 도는 호스트 종류입니다. 호스트마다 그 의존도 돌아야 합니다(정렬이 검사). */
        virtual SubsystemHostMask getSubsystemHost() const { return SubsystemHostMask::All; }
        /**
         * @brief 이 서브시스템이 할당하는 메모리의 용도입니다. 만드는 동안(`SubsystemScope::emplaceSubsystem`) · 초기화 · 재시작을 부르는 동안 이 태그가 걸립니다.
         * @details 값은 형식 상수 `kSubsystemMemoryTag` 하나다(파생이 같은 이름으로 가린다) — 컬렉션에 넣을 때 그 형식의 값을 붙인다(`SubsystemCollection::add`).
         *          예전에는 가상 함수와 별개로 만드는 쪽이 `SW_MEMORY_SCOPE` 를 손으로 다시 걸었다(같은 값을 두 곳에 적었다).
         */
        MemoryTag getSubsystemMemoryTag() const { return _memoryTag; }
        /** @brief 기본 용도입니다. 파생이 `static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::…;` 로 가립니다. */
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::EngineMisc;
        /** @brief 기본은 제 범위에만 보인다. 에디터가 찾는 호스트 서브시스템이 `static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::HostModules;` 로 가린다. */
        static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::ScopeOnly;

    private:
        friend class SubsystemCollection;
        MemoryTag _memoryTag{ MemoryTag::EngineMisc }; ///< 컬렉션이 넣을 때 붙인 형식의 `kSubsystemMemoryTag`
    };

    /** @brief 형식 @p T 가 재정의한 게임 인스턴스 상태 함수의 수입니다(0 ~ 2). 서브시스템이 아닌 형식(인터페이스만 거는 등록 줄)은 0 이다. */
    template <typename T, typename = void>
    inline constexpr uint32 kGameInstanceStateOverrideCount = 0;
    template <typename T>
    inline constexpr uint32 kGameInstanceStateOverrideCount<T, std::enable_if_t<std::is_base_of_v<Subsystem, T>>> =
        ( std::is_same_v<decltype( &T::resetGameInstanceState ), decltype( &Subsystem::resetGameInstanceState )> ? 0u : 1u ) +
        ( std::is_same_v<decltype( &T::findGameInstanceState ), decltype( &Subsystem::findGameInstanceState )> ? 0u : 1u );

    /** @brief 등록 줄이 고른 수명과 형식이 맞으면 true 입니다 — `GameInstance` 는 둘 다 재정의하고 `Process` 는 하나도 재정의하지 않는다. */
    template <typename T, SubsystemLifetime lifetime>
    inline constexpr bool kSubsystemLifetimeMatches = kGameInstanceStateOverrideCount<T> == ( lifetime == SubsystemLifetime::GameInstance ? 2u : 0u );
} // namespace sw
