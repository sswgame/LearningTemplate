/**
 * @file EditorVirtualList.h
 * @brief 길이가 묶이지 않는 목록(기록 · 검사 결과 · 전역 변수 · 팔레트 · 로그 · 표)을 보이는 줄만 그리는 공용 위젯입니다. 화면 밖 줄은 높이만 둔다.
 * @details 목록을 그리는 길이 하나여야 가상화를 빠뜨리지 않는다 — Slate `SListView` · UI Toolkit `ListView` 는 가상화가 기본이라 안 쓰는 길이 없다. ImGui 는 손으로 켜야 해서
 *          패널마다 `ImGuiListClipper` 를 직접 쓰거나(일부) 전부 그렸다(나머지). 계층 · 프로퍼티 그리드는 ViewModel 이 줄 높이 구간을 내는 다른 길이다(`HierarchyViewModel`).
 *          shortcut: 줄 높이가 같은 목록만 받는다(ImGui 클리퍼가 첫 줄로 높이를 잰다) — 줄마다 높이가 다른 목록은 ViewModel 이 높이 구간을 내는 길(계층)로 옮길 때 이 위젯이 구간을 받게 넓힌다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"

struct ImGuiListClipper;

namespace sw::editor
{
    /**
     * @class EditorVirtualList
     * @brief 줄 @p rowCount 개 가운데(번호는 ImGui 와 같은 int32) 이번 프레임에 보이는 구간만 돌게 합니다. `while ( list.step() ) for ( row = list.getBegin(); row < list.getEnd(); ++row )` 로 쓴다.
     * @details 한 프레임 한 목록에 하나를 쓰고 버린다. 키보드로 고른 줄처럼 화면 밖이어도 그려야 하는 줄은 `includeRow` 로 넣는다(`SetScrollHereY` 가 그 줄에서 돈다).
     */
    class SW_EDITOR_API EditorVirtualList final
    {
    public:
        /** @param rowHeight 줄 높이(픽셀). 0 이면 첫 줄을 그려 잰다 — 타일 격자처럼 한 줄이 여러 항목이면 넘긴다. */
        explicit EditorVirtualList( int32 rowCount, float32 rowHeight = 0.0f );
        ~EditorVirtualList();
        EditorVirtualList( const EditorVirtualList& )            = delete;
        EditorVirtualList& operator=( const EditorVirtualList& ) = delete;

        /** @brief 화면 밖이어도 이 줄을 그리게 합니다(첫 `step` 전에 부른다). 범위 밖 번호는 무시한다. */
        void includeRow( int32 rowIndex );
        /** @brief 다음 구간으로 갑니다. 그릴 구간이 더 없으면 false 입니다. */
        [[nodiscard]] bool step();
        /** @brief 이번 구간의 첫 줄입니다. */
        int32 getBegin() const { return _begin; }
        /** @brief 이번 구간의 끝(포함하지 않음)입니다. */
        int32 getEnd() const { return _end; }

    private:
        unique_ptr<ImGuiListClipper> _pClipper;
        int32                        _rowCount;
        int32                        _begin;
        int32                        _end;
    };
} // namespace sw::editor
