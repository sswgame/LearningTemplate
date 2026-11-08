/**
 * @file EditorAssetIndexEntry.h
 * @brief Resource 트리 분류의 결과 값 — 퀵 런처 항목과 리소스 카탈로그 개수(분류 동작은 `EditorAssetIndex.h`).
 * @details 값만 둔다: 백그라운드 잡(`EditorBackgroundIO.h`)과 패널 Model 이 결과를 값으로 들 때 분류 · 디스크 동작을 끌어오지 않게 가른다(MVVM View include 계약, `docs/agents/EditorUI.md`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw::editor
{
    /** @brief Resource 폴더 스캔으로 만든 퀵 런처용 파일 항목 */
    struct EditorAssetIndexEntry
    {
        EditorAssetType _kind{};   ///< 애셋 종류(`EditorAssetTypeRegistry::findKind`)
        string          _category; ///< 종류의 단수 이름(`EditorAssetTypeRegistration::_pDisplayName`)
        string          _title;
        string          _detail;
        string          _path; ///< 리소스 id(콘텐츠 브라우저 · 끌어 놓기와 같은 형태)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 리소스 카탈로그의 종류 하나 */
    struct EditorResourceCatalogCount
    {
        EditorAssetType _kind{};
        const utf8*     _pLabel{ nullptr }; ///< 종류의 브라우저 라벨(`EditorAssetTypeRegistration::_pBrowserLabel`)
        size_t          _count{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로파일러 리소스 카탈로그 — 종류 등록의 순서대로 종류마다 한 줄 */
    struct EditorResourceCatalogCounts
    {
        vector<EditorResourceCatalogCount> _listKindCount;
    };
} // namespace sw::editor
