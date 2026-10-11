/**
 * @file UINavigationSolver.h
 * @brief 포커스 탐색의 순수 계산입니다. 받을 수 있는지, 공간 점수, 방향과 탭 순서로 다음 위젯을 고르는 규칙을 정의합니다.
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
     * @struct UINavigationSolver
     * @brief 포커스 탐색에서 다음 위젯을 고르는 규칙입니다(상태가 없으며 테스트가 직접 부릅니다).
     * @details 고르는 순서는 다음과 같다(언리얼 FNavigationConfig, EUINavigationRule, Godot focus_neighbor 와 같은 구조).
     *          (1) 포커스 위젯에서 조상으로 올라가며 그 방향 항목을 본다. 첫 Explicit 은 그 이름의 위젯이고(받을 수 없으면 다음 규칙), 첫 Stop 이나 Wrap 위젯이
     *          찾는 범위가 된다. 모두 Escape 면 범위는 트리 루트(화면)이며 모달 밖으로 나가지 않는다.
     *          (2) 범위 안에서 받을 수 있는 위젯(보임, 켜짐, `supportsFocus`, 자르는 조상 밖으로 완전히 나가지 않음) 중 공간 점수가 가장 낮은 것을 고른다. 같으면 중심
     *          거리, 그래도 같으면 문서 순서가 앞인 것이다(결정적).
     *          (3) 없고 범위 규칙이 Wrap 이면 반대쪽 끝에서 고른다. 반대 방향으로 가장 먼 후보이며 수직 틈이 0 인 것이 먼저다.
     *          (4) Next 와 Previous 는 범위 안에서 받을 수 있는 위젯을 문서 순서로 늘어놓고 다음 또는 앞의 것을 고른다. 끝이면 처음으로 돌아간다(탭은 항상 돈다).
     */
    struct SW_API UINavigationSolver
    {
        /** @brief 포커스를 받을 수 있으면 true 입니다. `supportsFocus` 이고, 자기와 조상이 모두 켜져 있고 보여야 합니다(Collapsed 나 Hidden 이 아님). */
        static bool canReceiveFocus( const Widget& widget );
        /**
         * @brief 공간 탐색 점수입니다. 낮을수록 가깝고, 그 방향에 있지 않으면 음수(후보 아님)입니다.
         * @details 주축 거리(현재 사각형의 그 방향 변에서 후보의 맞은편 변까지이며, 겹치면 0) + 2 × 수직축 틈(두 구간이 겹치면 0)이다. 수직축에 벌점을 더 주는 것은
         *          "같은 줄이나 같은 열을 먼저" 라는 사람의 기대에 맞추기 위해서다.
         */
        static float32 computeSpatialScore( const UIRect& from, const UIRect& candidate, UINavigationDirection direction );
        /** @brief @p from 에서 @p direction 으로 옮길 위젯을 반환합니다. 없으면 무효 번호이며 그 자리에 남습니다. */
        static WidgetID findNextWidget( const WidgetTree& tree, WidgetID from, UINavigationDirection direction );
        /** @brief @p scope 아래(자기 포함)에서 문서 순서로 처음 받을 수 있는 위젯을 반환합니다. 없으면 무효 번호입니다. */
        static WidgetID findFirstFocusable( const Widget& scope );
    };
} // namespace sw
