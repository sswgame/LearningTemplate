/**
 * @file GameInstanceSubsystem.h
 * @brief 게임 인스턴스 범위 서브시스템의 공통 기반입니다. 대상(모든 호스트), 메모리 태그(`Game`), 빈 초기화와 종료를 정해 둡니다.
 * @details 언리얼 `UGameInstanceSubsystem` 에 해당합니다. `GameInstance` 가 게임 범위(`SubsystemScope`)에 넣어 소유하고,
 *          `initialize` 에서 파생의 `onInitialize` 앞에 초기화하며 `shutdown` 에서 `onShutdown` 뒤에 역순으로 종료합니다.
 *          게임과 키트 코드는 `game::findSubsystem<T>()` 로 찾습니다. 하위 클래스는 이 기반을 **첫 번째** 기반으로 상속하고 이름과, 필요하면 초기화와 종료만 적습니다.
 *          프레임마다 도는 핫 데이터나 게임플레이 객체(컴포넌트, 카탈로그 값)는 서브시스템이 아닙니다. 카탈로그는 `bindScopedService` 로 빌려 등록합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/MemoryTag.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class GameInstanceSubsystem
     * @brief 게임 인스턴스가 소유하는 서브시스템의 기반입니다. 이름(`getSubsystemName`)만 반드시 적습니다.
     */
    class GameInstanceSubsystem : public ISubsystem
    {
    public:
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        MemoryTag           getSubsystemMemoryTag() const override { return MemoryTag::Game; }
    };
} // namespace sw
