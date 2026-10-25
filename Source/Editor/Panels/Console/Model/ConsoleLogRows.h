/**
 * @file ConsoleLogRows.h
 * @brief Output Log 의 행 접기, 자동 스크롤, 여러 행 선택을 판단합니다. ImGui 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Log/LogTypes.h"

namespace sw::editor
{
    /** @brief Output Log 가 그리는 행 하나입니다. 접기를 켜면 같은 행 여럿이 처음 나온 위치의 한 행이 됩니다. */
    struct ConsoleLogRow
    {
        const LogEntry* _pEntry{ nullptr }; ///< 처음 나온 항목이다. 접기를 끄면 그 항목 자신이다
        uint32          _repeatCount{ 1 };  ///< 이 행으로 접힌 항목 수
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ConsoleLogRows
     * @brief 필터링한 로그 항목으로 행을 만들고, 스크롤과 선택을 판정합니다.
     * @details 접기는 유니티 Console 의 Collapse 와 같다. 수준, 태그, 카테고리, 메시지가 같은 항목은 떨어져 있어도 한 행이 된다.
     *          시각과 파일 위치는 비교하지 않는다.
     */
    struct ConsoleLogRows
    {
        /** @brief @p listVisible 로 @p outListRow 를 채웁니다. 먼저 출력 목록을 비웁니다. @p bCollapse 이면 같은 행을 처음 나온 위치에 접고 개수를 셉니다. */
        static void populate( const vector<const LogEntry*>& listVisible, bool bCollapse, vector<ConsoleLogRow>& outListRow );
        /**
         * @brief 스크롤이 맨 아래에 붙어 있는지 확인합니다. 붙어 있을 때만 새 로그를 따라 내려갑니다.
         * @details 위로 올려 읽는 중에는 새 로그가 와도 끌려 내려가지 않고, 다시 맨 아래로 내리면 따라가기가 돌아온다. 언리얼 Output Log 와 같다.
         */
        static bool isScrolledToBottom( float32 scrollY, float32 scrollMaxY, float32 tolerance );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ConsoleLogSelection
     * @brief Output Log 의 여러 행 선택입니다. 누른 행이 기준(anchor)이고, 드래그하거나 Shift 로 누른 행까지가 선택입니다.
     */
    class ConsoleLogSelection
    {
    public:
        /** @brief 선택이 없는 상태로 만듭니다. */
        ConsoleLogSelection();

        /** @brief @p rowIndex 를 누릅니다. @p bExtend 이면 기준은 그대로 두고 끝만 옮깁니다(Shift). */
        void press( uint32 rowIndex, bool bExtend );
        /** @brief 누른 채 드래그해 @p rowIndex 까지 넓힙니다. 누르고 있지 않으면 아무 일도 하지 않습니다. */
        void dragTo( uint32 rowIndex );
        /** @brief 마우스를 뗍니다. 선택은 남습니다. */
        void release() { _bDragging = false; }
        /** @brief 선택을 지웁니다. */
        void clear();
        /** @brief 행 수가 @p rowCount 로 줄었을 때 범위를 그 안으로 자릅니다. 범위가 모두 밖이면 지웁니다. */
        void clampTo( uint32 rowCount );

        /** @brief 선택이 있으면 true 입니다. */
        bool hasSelection() const { return _bHasSelection; }
        /** @brief 드래그 중이면 true 입니다. */
        bool isDragging() const { return _bDragging; }
        /** @brief @p rowIndex 가 선택 안이면 true 입니다. */
        bool isSelected( uint32 rowIndex ) const;
        /** @brief 선택의 첫 행입니다. */
        uint32 getFirst() const;
        /** @brief 선택의 끝 행입니다. 이 행을 포함합니다. */
        uint32 getLast() const;
        /** @brief 선택한 행 수입니다. */
        uint32 getCount() const { return _bHasSelection ? getLast() - getFirst() + 1 : 0; }

    private:
        uint32 _anchorRow;
        uint32 _activeRow;
        bool   _bHasSelection;
        bool   _bDragging;
    };
} // namespace sw::editor
