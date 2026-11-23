/**
 * @file Editor/Common/EditorSubsystem.h
 * @brief 에디터 서브시스템의 공통 기반입니다. 대상(`Client`) · 메모리 태그(`Editor`) · 빈 의존 · 빈 초기화와 종료를 정해 둡니다.
 * @details 언리얼 `UEditorSubsystem` 에 해당합니다. `EditorContext` 가 하위 객체를 `SubsystemCollection` 으로 소유해, 선언된 의존으로 정렬한
 *          순서로 초기화하고 역순으로 종료 · 소멸합니다. 하위 객체는 이 기반을 **첫 번째** 기반으로 상속하고 이름과, 필요하면 의존 · 초기화 · 종료만 적습니다.
 *          초기화는 호스트의 `Editor` 메모리 범위를 벗어나 서브시스템의 태그 아래에서 돌므로 태그를 여기서 `Editor` 로 고정합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/MemoryTag.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"

namespace sw::editor
{
    /**
     * @class EditorSubsystem
     * @brief 에디터 하위 객체(`EditorContext` 가 소유)의 서브시스템 기반입니다. 이름(`getSubsystemName`)만 반드시 적습니다.
     */
    class EditorSubsystem : public Subsystem
    {
    public:
        SubsystemInitResult        initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                       shutdownSubsystem() override {}
        SubsystemHostMask          getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Editor;

        /**
         * @brief 새로 띄운 에디터와 같은 자리로 되돌립니다 — 앞 시나리오 단계가 남긴 값(요청 줄 · 선택 · 보기 상태)을 비웁니다. 등록 · 장치 · 디스크와 맞춘 값은 둡니다.
         * @details 자동화 체인의 단계 되돌리기가 `EditorContext::resetToFreshEditor` 로 모두 부른다. 순수 가상이라 새 서브시스템은 무엇을 되돌릴지 정해야 컴파일된다 —
         *          되돌릴 목록을 한 파일에 손으로 적으면 새 상태가 빠져, 앞 단계의 뷰 · 기즈모 · 잠금이 다음 단계로 새었다.
         */
        virtual void resetToFreshEditor() = 0;
    };
} // namespace sw::editor
