/**
 * @file UiNavigationSolver.h
 * @brief 포커스 탐색의 순수 계산입니다 — 받을 수 있는가 · 공간 점수 · 방향/탭 순서로 다음 위젯 고르기.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/UI/Base/WidgetNavigation.h"
#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    class Widget;
    class WidgetTree;

    /**
     * @struct UiNavigationSolver
     * @brief 포커스 탐색의 고르기 규칙입니다(상태 없음 — 시험이 직접 부른다).
     * @details 고르기 순서(언리얼 FNavigationConfig · EUINavigationRule, Godot focus_neighbor 와 같은 모양):
     *          (1) 포커스 위젯에서 조상으로 올라가며 그 방향 항목을 본다 — 첫 Explicit 은 그 이름의 위젯(받을 수 없으면 다음 규칙), 첫 Stop · Wrap 위젯이
     *          **찾는 범위**. 모두 Escape 면 범위 = 트리 뿌리(화면 — 모달 밖으로 나가지 않는다).
     *          (2) 범위 안 받을 수 있는 위젯(보임 · 켜짐 · `supportsFocus` · 자르는 조상 밖으로 완전히 나가지 않음) 중 공간 점수가 가장 낮은 것. 같으면 중심
     *          거리, 그래도 같으면 문서 순서가 앞인 것(결정적).
     *          (3) 없고 범위 규칙이 Wrap 이면 반대쪽 끝 — 반대 방향으로 가장 먼 후보(수직 틈이 0 인 것 먼저).
     *          (4) Next · Previous 는 범위 안 받을 수 있는 위젯을 문서 순서로 늘어놓고 다음 · 앞, 끝이면 처음으로(탭은 늘 돈다).
     */
    struct SW_API UiNavigationSolver
    {
        /** @brief 포커스를 받을 수 있으면 true 입니다 — `supportsFocus`, 자기와 조상이 모두 켜져 있고 보인다(Collapsed · Hidden 이 아니다). */
        static bool canReceiveFocus( const Widget& widget );
        /**
         * @brief 공간 탐색 점수입니다. 낮을수록 가깝고, 그 방향에 있지 않으면 음수(후보 아님)입니다.
         * @details 주축 거리(현재 사각형의 그 방향 변 → 후보의 맞은편 변, 겹치면 0) + 2 × 수직축 틈(두 구간이 겹치면 0). 수직축에 벌을 더 주는 것은
         *          "같은 줄 · 같은 열을 먼저" 라는 사람의 기대다.
         */
        static float32 computeSpatialScore( const UiRect& from, const UiRect& candidate, UiNavigationDirection direction );
        /** @brief @p from 에서 @p direction 으로 옮길 위젯입니다. 없으면 무효 번호입니다(그대로 선다). */
        static WidgetID findNextWidget( const WidgetTree& tree, WidgetID from, UiNavigationDirection direction );
        /** @brief @p scope 아래(자기 포함)에서 문서 순서로 처음 받을 수 있는 위젯입니다. 없으면 무효 번호입니다. */
        static WidgetID findFirstFocusable( const Widget& scope );
    };
} // namespace sw
