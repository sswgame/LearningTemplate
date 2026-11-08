/**
 * @file ProjectSettingsPage.h
 * @brief 프로젝트 설정 창의 파일 하나와 그 편집 상태입니다. ImGui 없이 값만 다룹니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsDocument.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsEntry.h"

namespace sw::editor
{
    /** @brief 감시 값이 읽는 지난 프레임의 값입니다. 패널 인스턴스는 감시 값에서 보이지 않으므로 에디터 범위의 상태 저장소가 소유한다(`ProjectSettingsPage::getWatchValues`). */
    struct ProjectSettingsWatchValues
    {
        uint32 _pageCount{ 0 };
        uint32 _selectedKeyCount{ 0 };
        uint32 _dirtyPageCount{ 0 };
    };

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
        /**
         * @brief 창의 목록을 다시 만듭니다(@p outListPage 를 비우고 채운다): 활성 팩의 `<Engine>` 요소가 맨 앞, 다음은 설정 테이블이 파일에서 읽은 설정입니다(`ProjectSettingsEntry::collectLoaded`).
         * @details 팩 폴더를 찾지 못했거나 설정 관리자가 없으면 그 페이지를 만들지 않고 오류를 남긴다(빈 루트에 이어 붙이면 작업 폴더의 파일을 열고 쓴다).
         */
        static void collectPages( vector<unique_ptr<ProjectSettingsPage>>& outListPage );
        /** @brief 에디터 범위의 감시 값 칸입니다. 창이 그리기마다 채운다. */
        static ProjectSettingsWatchValues& getWatchValues();
    };
} // namespace sw::editor
