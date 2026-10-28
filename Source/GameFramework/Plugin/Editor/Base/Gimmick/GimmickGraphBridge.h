/**
 * @file GimmickGraphBridge.h
 * @brief `GimmickGraphModel` 과 `GimmickCircuitComponent` 사이의 읽기 · 쓰기입니다. 컴포넌트의 저장 목록(비공개 PROPERTY)을 리플렉션으로 읽고 씁니다.
 * @details 컴포넌트는 노드 · 대상 · 배선 목록을 getter 없이 비공개 PROPERTY 로만 둔다(인스펙터와 직렬화기가 리플렉션으로 만진다). 에디터도 같은 길을 쓴다 — 컴포넌트 헤더에
 *          편집기 전용 getter 를 더하지 않는다. 쓴 뒤에는 컴포넌트의 `onPropertyChanged` 를 불러 회로를 다시 짓는다(인스펙터가 속성을 고친 것과 같은 알림).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/Plugin/Editor/Base/Gimmick/GimmickGraphModel.h"

namespace sw
{
    class GimmickCircuitComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct GimmickGraphBridge
     * @brief 모델 ↔ 컴포넌트 변환입니다.
     */
    struct GimmickGraphBridge
    {
        /** @brief 컴포넌트가 저장한 목록으로 @p outModel 을 채웁니다. 리플렉션에서 속성을 못 찾으면 false 입니다. */
        [[nodiscard]] static bool load( const GimmickCircuitComponent& component, GimmickGraphModel& outModel );
        /** @brief 모델의 목록을 컴포넌트에 쓰고 회로를 다시 짓습니다. 속성을 못 찾으면 false 이고 컴포넌트는 그대로입니다. */
        [[nodiscard]] static bool apply( GimmickCircuitComponent& component, const GimmickGraphModel& model );
        /** @brief 컴포넌트가 저장한 내용의 서명입니다(`GimmickGraphModel::computeSignature` 와 같은 값). 읽지 못하면 0 입니다. 에디터가 바깥 변경(되돌리기 · 인스펙터)을 알아볼 때 부릅니다. */
        static uint64 computeSignature( const GimmickCircuitComponent& component );
    };
} // namespace sw::editor
