/**
 * @file Editor/Common/EditorSubsystem.h
 * @brief 에디터 서브시스템의 공통 기반입니다. 대상(`Client`) · 메모리 태그(`Editor`) · 빈 의존 · 빈 초기화와 종료를 정해 둡니다.
 * @details 언리얼 `UEditorSubsystem` 과 같은 자리입니다. `EditorContext` 가 하위 객체를 `SubsystemCollection` 으로 소유해, 선언된 의존으로 정렬한
 *          순서로 초기화하고 역순으로 종료 · 소멸합니다. 하위 객체는 이 기반을 **첫 번째** 기반으로 상속하고 이름과, 필요하면 의존 · 초기화 · 종료만 적습니다.
 *          초기화는 호스트의 `Editor` 메모리 범위를 벗어나 서브시스템의 태그 아래에서 돌므로 태그를 여기서 `Editor` 로 고정합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/MemoryTag.h"

#include "Engine/Common/ISubsystem.h"

namespace sw::editor
{
    /**
     * @class EditorSubsystem
     * @brief 에디터 하위 객체(`EditorContext` 가 소유)의 서브시스템 기반입니다. 이름(`getSubsystemName`)만 반드시 적습니다.
     */
    class EditorSubsystem : public ISubsystem
    {
    public:
        EngineInitResult initializeSubsystem() override { return EngineInitResult::Succeeded; }
        void             shutdownSubsystem() override {}
        const utf8*      getSubsystemDependencies() const override { return ""; }
        EngineInitTarget getSubsystemTarget() const override { return EngineInitTarget::Client; }
        MemoryTag        getSubsystemMemoryTag() const override { return MemoryTag::Editor; }
    };
} // namespace sw::editor
