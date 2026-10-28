/**
 * @file EditorTextExtent.h
 * @brief 창 · 패널 · 팝업의 크기를 글자 칸 수와 줄 수로 적는 형식입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @struct EditorTextExtent
     * @brief 글자 너비(숫자 "0" 의 폭)의 배수와 글자 줄 높이(`GetTextLineHeightWithSpacing`)의 배수로 적은 창 크기입니다.
     * @details 픽셀 값으로는 만들 수 없다(생성자가 `ofText` 뿐이다). 픽셀 리터럴로 정한 창은 글꼴 크기 · UI 배율이 바뀌면 내용과 어긋나고
     *          창 높이와도 무관해서 낮은 창에서 본문이 한두 줄만 남았다. 픽셀로 바꾸는 곳은 `EditorChrome::resolveTextExtent` 하나이고,
     *          거기서 주 뷰포트 작업 영역의 일정 비율로 자른다. 칸 또는 줄이 0 이하이면 그 축은 정하지 않는다(내용 크기를 따른다).
     */
    struct EditorTextExtent
    {
        /** @brief 글자 칸 수 @p columns 와 줄 수 @p lines 로 크기를 적습니다. */
        [[nodiscard]] static constexpr EditorTextExtent ofText( float32 columns, float32 lines ) { return EditorTextExtent{ columns, lines }; }

        float32 _columns;
        float32 _lines;

    private:
        constexpr EditorTextExtent( float32 columns, float32 lines )
            : _columns{ columns }
            , _lines{ lines }
        {
        }
    };
} // namespace sw::editor
