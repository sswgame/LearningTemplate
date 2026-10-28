/**
 * @file UIFocusManager.h
 * @brief 사용자(로컬 플레이어) 하나의 포커스를 관리합니다. 어느 트리의 어느 위젯이 포커스를 가졌는지, 옮기기, 방향 탐색을 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/UI/WidgetTree/WidgetNavigation.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    struct UIWidgetPath;

    class WidgetTree;

    /**
     * @class UIFocusManager
     * @brief 포커스는 사용자마다 하나입니다(언리얼 FSlateUser 의 포커스 경로에 해당). 지금은 사용자 0 하나뿐이며, 분할 화면 사용자를 늘리면 관리자를 사용자마다 둡니다.
     * @details 포커스 위젯 번호는 그 트리에 적힌다(`WidgetTree::getFocusedWidget`). 위젯이 떨어지면 트리가 지우므로 여기서 따로 지울 일이 없다.
     *          포커스가 바뀌면 기존 위젯과 새 위젯에 kStyle(`:focus` 상태) 무효화와 `onFocusChanged` 를 부른다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIFocusManager
    {
    public:
        UIFocusManager();
        ~UIFocusManager();
        UIFocusManager( const UIFocusManager& )            = delete;
        UIFocusManager& operator=( const UIFocusManager& ) = delete;

        /** @brief @p widget(@p tree 안)으로 포커스를 옮깁니다. 받을 수 없는 위젯(`supportsFocus` 가 false 이거나 꺼짐, 안 보임)이면 false 를 반환하고 그대로 둡니다. */
        [[nodiscard]] bool setFocus( WidgetTree& tree, WidgetID widget );
        /** @brief 포커스를 해제합니다(어느 트리에 있든). */
        void clearFocus();
        /** @brief 지금 포커스 위젯입니다(없으면 무효). */
        WidgetID getFocusedWidget() const;
        /** @brief 포커스가 있는 트리입니다(없으면 nullptr). */
        WidgetTree* getFocusedTree() const { return _pFocusedTree; }
        /**
         * @brief @p tree 안에서 방향으로 다음 위젯을 찾아 옮깁니다(`UINavigationSolver::findNextWidget`). 포커스가 그 트리에 없거나 못 찾으면 그대로 두고 false 를 반환합니다.
         * @details 옮기면 새 포커스의 조상 패널마다 `scrollIntoView` 를 부른다(스크롤 패널이 보이게 옮기며, UMG ScrollBox 의 탐색 스크롤에 해당한다).
         */
        [[nodiscard]] bool navigate( WidgetTree& tree, UINavigationDirection direction );
        /** @brief 포커스 위젯에서 루트까지의 경로(루트에서 포커스 위젯)입니다. 액션 이벤트가 이 경로를 탑니다. 포커스가 @p tree 에 없으면 빈 경로입니다. */
        void makeFocusPath( const WidgetTree& tree, UIWidgetPath& outPath ) const;
        /** @brief @p tree 를 더 쓰지 않습니다. 그 트리에 있던 포커스를 알림 없이 버립니다(트리가 지워질 때 트리가 부릅니다). */
        void forgetTree( const WidgetTree& tree );

    private:
        WidgetTree* _pFocusedTree;
    };
} // namespace sw
