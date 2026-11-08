/**
 * @file ProjectSettingsPage.h
 * @brief 프로젝트 설정 창의 파일 하나와 그 편집 상태입니다. ImGui 없이 값만 다룹니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsDocument.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsEntry.h"

namespace sw::editor
{
    /**
     * @struct ProjectSettingsPage
     * @brief 목록의 파일 하나입니다. 처음 고를 때 엽니다. 저장 · 되돌리기 · 기본값이 저장하지 않은 표시를 함께 맞춥니다.
     */
    struct ProjectSettingsPage
    {
        ProjectSettingsEntry    _entry;
        ProjectSettingsDocument _document;
        string                  _absolutePath;
        bool                    _bOpened{ false };    ///< NOLINT(sw-bool-flag): 집합체다
        bool                    _bTypeFound{ false }; ///< NOLINT(sw-bool-flag): 집합체다
        bool                    _bDirty{ false };     ///< NOLINT(sw-bool-flag): 집합체다

        /** @brief 파일을 엽니다. 형식을 찾지 못하면 `_bTypeFound` 를 내립니다. */
        void open();
        /** @brief 문서와 비교해 저장하지 않은 표시를 다시 정합니다. */
        void refreshDirty();
        /** @brief 파일에 쓰고 호스트에 알립니다. 호스트가 반영했으면 @p outApplied 가 true 입니다. 쓰지 못하면 false 이고 표시는 그대로입니다. */
        [[nodiscard]] bool saveDocument( bool& outApplied );
        /** @brief 파일 내용으로 되돌립니다. 결과는 문서의 상태로 보입니다. */
        void revert();
        /** @brief 모든 값을 기본값으로 바꿉니다. 저장해야 파일에 씁니다. */
        void resetToDefault();

        /** @brief 저장하지 않은 쪽이 하나라도 있으면 true 입니다. */
        static bool hasDirty( const vector<unique_ptr<ProjectSettingsPage>>& listPage );
    };
} // namespace sw::editor
