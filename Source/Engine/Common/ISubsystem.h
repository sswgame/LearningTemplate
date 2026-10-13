/**
 * @file Engine/Common/ISubsystem.h
 * @brief 엔진 서브시스템의 계약입니다. 스스로 이름 · 의존 · 대상을 선언하고 초기화 · 종료를 구현합니다.
 * @details 언리얼 `USubsystem`(`Initialize` · `Deinitialize` · `InitializeDependency`)과 같은 자리입니다. 순서는 손으로 적지 않습니다 —
 *          `SubsystemCollection` 이 선언된 의존으로 위상 정렬해(동점은 이름 순) 초기화하고, 역순으로 멈춘(stop) 뒤 역순으로 종료하고, 소멸도 그 역순입니다.
 *          새 서브시스템은 이 인터페이스를 상속하고 엔진 루프가 `EngineInitSequence::addSubsystem` 으로 넘기면 끝납니다(표 · 본문 함수가 없다).
 *          자기 클래스가 없는 단계(호스트가 엮는 일, Core 서비스를 세우는 일)는 `FunctionSubsystem` 하나로 적습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/MemoryTag.h"

namespace sw
{
    /** @brief 서브시스템(기동 단계)이 도는 호스트입니다. 비트라 `All` 은 둘 다입니다. */
    enum class EngineInitTarget : uint8
    {
        Client = 1 << 0, ///< 창 · 렌더러 · 플레이어 설정이 있는 호스트(App · 에디터 · 테스트 하네스)
        Server = 1 << 1, ///< 전용 서버(Server 실행 파일)
        All    = Client | Server,
    };
} // namespace sw

namespace sw
{
    /** @brief 서브시스템(기동 단계) 하나의 초기화 결과입니다. */
    enum class EngineInitResult : uint8
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
     * @brief 엔진 서브시스템입니다. `SubsystemCollection` 이 소유하고 의존 순서로 초기화 · 종료 · 소멸합니다.
     * @details - 이름은 다른 서브시스템의 의존 칸이 부르는 식별자입니다. 문자열 리터럴(프로세스 수명)을 돌려줍니다.
     *          - 의존은 먼저 서야 하는 서브시스템 이름들입니다(쉼표 · 공백 · 중괄호로 구분). 모르는 이름 · 순환 · 대상 어긋남은 정렬 오류입니다.
     *          - `initializeSubsystem` 은 백엔드 교체(`shutdownDependentsOf` → `restartStopped`)로 다시 불릴 수 있습니다 — 이미 있는 것은 다시 씁니다.
     *          - 종료는 두 번 역순으로 돕니다. 먼저 `stopSubsystem`(종료 전 단계 — 새 일 받기를 멈추고 남은 일을 비운다)이 모두에 불리고,
     *            그다음 `shutdownSubsystem` 이 모두에 불립니다(언리얼의 PreExit 뒤 `Deinitialize`). 둘 다 **초기화에 성공한** 서브시스템에만,
     *            성공한 초기화 한 번마다 한 번씩 불립니다. 이때 다른 서브시스템은 아직 모두 살아 있습니다.
     *          - 소멸자는 모든 종료 뒤에 **모든** 서브시스템에(초기화하지 않았어도) 역순으로 불립니다.
     *          클래스가 따로 가진 `initialize( … )` · `shutdown()`(테스트가 자기 인스턴스로 쓰는 것)과 이름을 나눠, 엔진이 부르는 자리만 이 둘입니다.
     * @note 다중 상속이면 이 인터페이스를 **첫 번째** 기반으로 둡니다 — 소유자가 이 포인터로 지우므로 객체 주소와 같아야 합니다(`SubsystemCollection::add` 가 단언).
     */
    class ISubsystem
    {
    public:
        virtual ~ISubsystem() = default;

        /** @brief 서브시스템을 세웁니다. 위상 순서로 불리고, 그동안 `getSubsystemMemoryTag` 가 걸립니다. */
        virtual EngineInitResult initializeSubsystem() = 0;
        /**
         * @brief 종료 전 단계입니다 — 새 일 받기를 멈추고 남은 일을 비웁니다. 어느 `shutdownSubsystem` 보다도 먼저, 초기화에 성공한 것만 역순으로 불립니다.
         * @details 다른 서브시스템의 남은 일(완료 콜백)이 이 서브시스템을 부를 수 있으면 그 일을 여기서 비우고, 객체를 놓는 것은 `shutdownSubsystem` 에 둡니다.
         *          기본은 아무것도 하지 않습니다.
         */
        virtual void stopSubsystem() {}
        /** @brief 세운 서브시스템을 내립니다. 모든 `stopSubsystem` 뒤에, 초기화에 성공한 것만 역순으로 불립니다. */
        virtual void shutdownSubsystem() = 0;

        /** @brief 이름입니다(의존 칸이 부르는 식별자, 문자열 리터럴). */
        virtual const utf8* getSubsystemName() const = 0;
        /** @brief 먼저 서야 하는 서브시스템 이름들입니다(쉼표 · 공백 · 중괄호로 구분, 없으면 빈 글). */
        virtual const utf8* getSubsystemDependencies() const = 0;
        /** @brief 이 서브시스템이 도는 호스트입니다. 대상마다 그 의존도 돌아야 합니다(정렬이 검사). */
        virtual EngineInitTarget getSubsystemTarget() const = 0;
        /** @brief 초기화가 할당하는 메모리의 용도입니다. 초기화 · 재시작을 부르는 동안 이 태그가 걸립니다. */
        virtual MemoryTag getSubsystemMemoryTag() const { return MemoryTag::EngineMisc; }
    };
} // namespace sw
