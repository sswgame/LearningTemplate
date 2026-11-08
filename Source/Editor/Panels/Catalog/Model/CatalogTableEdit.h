/**
 * @file CatalogTableEdit.h
 * @brief 표에서 고른 편집 하나입니다 — 그리는 동안 값으로 들고 있다가 표를 다 그린 뒤 문서에 넣습니다(ImGui 없음).
 * @details 그리는 도중 문서의 원소 인덱스가 바뀌면 그리던 표가 깨지므로 위젯(`CatalogTableEditor`)은 이 값만 남긴다. 상태 줄 · 로그의 한 줄 설명도 여기서 만든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Panels/Catalog/Model/CatalogChildSection.h"

#include "Engine/Curve/FloatCurve.h"

namespace sw::editor
{
    class CatalogDocument;

    /** @struct CatalogTableEdit @brief 파일 머리말을 참고하세요. */
    struct CatalogTableEdit
    {
        enum class Kind : uint8
        {
            None = 0,
            SetCell,
            UnsetCell,
            AddRow,
            RemoveRow,
            DuplicateRow,
            CreateVariant, ///< `_element` 를 `_base` 로 가리키는 새 행
            AddCurveKey,   ///< `_element` 곡선의 끝에 키를 더한다
            SetCurveKeys,  ///< `_element` 곡선의 키를 `_curve`(그래프에서 끈 곡선)로 맞춘다
            // 자식 원소 편집 — `_element` 는 행, `_path` 는 원소(또는 담는 원소), `_column` 은 칸 이름(그룹 편집이면 원소 이름)이다.
            SetChildCell,
            UnsetChildCell,
            AddChildNode, ///< `_text` 는 열쇠 칸 이름
            RemoveChildNode,
            RestoreChildNode,
            MoveChildEarlier,
            MoveChildLater,
            ResetChildOrder,
            ClearChildGroup,
            InheritChildGroup,
            OverrideChildGroup
        };

        Kind             _kind{ Kind::None };
        int32            _element{ -1 };
        string           _column{};
        string           _text{}; ///< 칸의 새 값(`SetCell`) 또는 표 이름(`AddRow`)
        CatalogChildPath _path{};
        FloatCurve       _curve{}; ///< `SetCurveKeys` 의 고친 곡선

        /**
         * @brief 문서에 넣습니다.
         * @param outDescription 바뀌었으면 한 줄 설명(`Edited Item.cost`), 실패했으면 문서가 말한 이유입니다.
         * @return 문서가 바뀌었으면 true 입니다.
         */
        [[nodiscard]] bool apply( CatalogDocument& inoutDocument, string& outDescription ) const;
    };
} // namespace sw::editor
