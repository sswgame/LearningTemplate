/**
 * @file EditorPanelOpenRequest.h
 * @brief 패널 열기 요청 하나의 값입니다(`EditorWorkspace` 가 쌓고 메뉴 막대가 꺼내 연다). 작업 공간 헤더 없이 읽히도록 값만 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetTypeID.h"

namespace sw::editor
{
    /** @brief 패널 열기 요청 하나입니다. 패널 id 와 문서 종류 중 하나만 채웁니다(`EditorWorkspace::requestOpenPanel<T>` · `requestOpenDocumentPanel`). */
    struct EditorPanelOpenRequest
    {
        string          _panelID;            ///< 등록 id(`T::kPanelID`) 또는 실행 중에 들어온 id
        EditorAssetType _documentKind{};     ///< 이 종류의 문서를 여는 도구 패널. 비면 id 로 연다
        bool            _bFloat{ SW_FALSE }; ///< 도킹을 풀고 메인 창 밖으로 옮긴다 — ImGui 가 그 패널을 제 OS 창(보조 뷰포트)으로 띄운다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor
