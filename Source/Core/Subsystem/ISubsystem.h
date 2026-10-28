/**
 * @file Core/Subsystem/ISubsystem.h
 * @brief 서브시스템의 계약입니다. 스스로 이름 · 의존 · 도는 호스트를 선언하고 초기화 · 종료를 구현합니다.
 * @details 언리얼 `USubsystem`(`Initialize` · `Deinitialize` · `InitializeDependency` · `ShouldCreateSubsystem`)과 같은 자리입니다. 순서는 손으로 적지 않습니다 —
 *          `SubsystemCollection` 이 선언된 의존으로 위상 정렬해(동점은 이름 순) 초기화하고, 역순으로 멈춘(stop) 뒤 역순으로 종료하고, 소멸도 그 역순입니다.
 *          초기화 · 종료 짝과 의존 순서는 엔진에 매인 개념이 아니라 Core 에 둔다 — Core 의 매니저(태스크 · 비동기 IO · 코덱 레지스트리)도 직접 상속한다.
 *          어떤 서브시스템이 있고 호스트 비트가 무엇을 뜻하는지(클라이언트 · 전용 서버)는 쓰는 쪽(엔진)이 정한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Memory/MemoryTag.h"

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
} // namespace sw

namespace sw
{
    /**
     * @class ISubsystem
     * @brief 서브시스템입니다. `SubsystemCollection` 이 소유하고 의존 순서로 초기화 · 종료 · 소멸합니다.
     * @details - 이름은 다른 서브시스템의 의존 칸이 부르는 식별자입니다. 문자열 리터럴(프로세스 수명)을 돌려줍니다. 엔진 서브시스템은 클래스의
     *            `kSubsystemName` 상수로 두고 의존 칸은 그 상수를 가리킨다 — 철자가 틀리면 컴파일 오류다.
     *          - 의존은 먼저 서야 하는 서브시스템 이름들입니다. 모르는 이름 · 순환 · 호스트 어긋남은 정렬 오류입니다.
     *          - `initializeSubsystem` 은 백엔드 교체(`shutdownDependentsOf` → `restartStopped`)로 다시 불릴 수 있습니다 — 이미 있는 것은 다시 씁니다.
     *          - 종료는 두 번 역순으로 돕니다. 먼저 `stopSubsystem`(종료 전 단계 — 새 일 받기를 멈추고 남은 일을 비운다)이 모두에 불리고,
     *            그다음 `shutdownSubsystem` 이 모두에 불립니다(언리얼의 PreExit 뒤 `Deinitialize`). 둘 다 **초기화에 성공한** 서브시스템에만,
     *            성공한 초기화 한 번마다 한 번씩 불립니다. 이때 다른 서브시스템은 아직 모두 살아 있습니다.
     *          - 소멸자는 모든 종료 뒤에 **모든** 서브시스템에(초기화하지 않았어도) 역순으로 불립니다.
     *          클래스가 따로 가진 `initialize( … )` · `shutdown()`(테스트가 자기 인스턴스로 쓰는 것)과 이름을 나눠, 컬렉션이 부르는 자리만 이 둘입니다.
     */
    class ISubsystem
    {
    public:
        ISubsystem()                               = default;
        virtual ~ISubsystem()                      = default;
        ISubsystem( const ISubsystem& )            = delete;
        ISubsystem& operator=( const ISubsystem& ) = delete;

        /** @brief 서브시스템을 세웁니다. 위상 순서로 불리고, 그동안 `getSubsystemMemoryTag` 가 걸립니다. */
        virtual SubsystemInitResult initializeSubsystem() = 0;
        /**
         * @brief 종료 전 단계입니다 — 새 일 받기를 멈추고 남은 일을 비웁니다. 어느 `shutdownSubsystem` 보다도 먼저, 초기화에 성공한 것만 역순으로 불립니다.
         * @details 다른 서브시스템의 남은 일(완료 콜백)이 이 서브시스템을 부를 수 있으면 그 일을 여기서 비우고, 객체를 놓는 것은 `shutdownSubsystem` 에 둡니다.
         */
        virtual void stopSubsystem() {}
        /** @brief 세운 서브시스템을 내립니다. 모든 `stopSubsystem` 뒤에, 초기화에 성공한 것만 역순으로 불립니다. */
        virtual void shutdownSubsystem() = 0;

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
        /** @brief 초기화가 할당하는 메모리의 용도입니다. 초기화 · 재시작을 부르는 동안 이 태그가 걸립니다. */
        virtual MemoryTag getSubsystemMemoryTag() const { return MemoryTag::EngineMisc; }
    };
} // namespace sw
