/**
 * @file EditorFolderListingEntry.h
 * @brief 콘텐츠 브라우저 폴더 목록의 항목 값(읽는 동작은 `EditorFolderListing.h`).
 * @details 값만 둔다: 백그라운드 잡(`EditorBackgroundIO.h`)과 패널 Model 이 목록을 값으로 들 때 디스크 동작을 끌어오지 않게 가른다(MVVM View include 계약, `docs/agents/EditorUI.md`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

namespace sw::editor
{
    /** @brief Content Browser 폴더의 항목 하나입니다. */
    struct EditorFolderListingEntry
    {
        string       _name;
        string       _relativePath;
        AbsolutePath _absolutePath;
        string       _extension;
        bool         _bIsDirectory{ false };
        bool         _bReadOnly{ false }; ///< 파일이 읽기 전용이다. 버전 관리가 잠그기 전이거나 사람이 막은 경우다. 목록을 만들 때 워커가 확인한다.
    };
} // namespace sw::editor
