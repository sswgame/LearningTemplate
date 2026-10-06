/**
 * @file EditorLabelLayout.h
 * @brief 이름표를 폭 안의 줄로 나누는 판단입니다(ImGui 에 의존하지 않아 테스트를 붙일 수 있습니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /**
     * @struct EditorLabelLayoutUtil
     * @brief 이름표를 폭 안의 줄로 나눕니다. 끊는 자리는 공백 · '_' · '-' · '.' 뒤가 먼저이고, 없을 때만 글자 단위입니다.
     * @details ImGui 의 TextWrap 은 공백만 보므로 `nilecity` 같은 이름이 글자 가운데서 끊깁니다. 그리는 쪽은
     *          `EditorWidgets::drawClampedLabel` 이고(마지막 줄 말줄임 · 전체 이름 툴팁), 여기는 어디서 끊는지만 정합니다.
     */
    struct EditorLabelLayoutUtil
    {
        /** @brief 글자열 너비를 재는 함수입니다(ImGui 는 CalcTextSize, 시험은 글자 수 × 고정 폭). */
        using MeasureFunc = float32 ( * )( string_view text, void* pUserData );

        /**
         * @brief @p text 를 @p width 안의 줄로 최대 @p maxLineCount 개 나눠 @p outListLine 에 채웁니다(먼저 비운다).
         * @details 마지막 줄은 넘쳐도 남은 전부다 — 말줄임은 그리는 쪽이 한다. 줄 머리의 공백은 버린다. 빈 글은 빈 줄 하나다.
         *          UTF-8 글자 가운데서 끊지 않는다.
         */
        static void breakLines( string_view text, float32 width, uint32 maxLineCount, MeasureFunc pfnMeasure, void* pUserData,
                                vector<string_view>& outListLine );
    };
} // namespace sw::editor
