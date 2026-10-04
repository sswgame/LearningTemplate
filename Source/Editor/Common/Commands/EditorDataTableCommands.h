/**
 * @file EditorDataTableCommands.h
 * @brief 로컬라이제이션 프로젝트(원문 표 · 문화권 번역 표) / 게임 데이터 XML 파일 IO 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Localization/LocalizationDocuments.h"

namespace sw::editor
{
    /** @brief 표 한 줄 — 키 하나의 원문 · 메타데이터와 문화권마다의 번역입니다. */
    struct LocalizationRecord
    {
        string                   _key;
        string                   _source;
        string                   _context;
        string                   _comment;
        vector<string>           _listTranslation; ///< `LocalizationSheet::_listCulture` 와 같은 순서
        vector<TranslationState> _listState;       ///< 읽었을 때의 상태(낡음 · 검토 · 없음)
        uint32                   _maxLength{ 0 };
        uint32                   _tableIndex{ 0 }; ///< 이 키가 사는 원문 표(`LocalizationSheet::_listTablePath`)
        bool                     _bModified{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로젝트 하나를 표로 편 것입니다. */
    struct LocalizationSheet
    {
        string                     _projectPath;
        string                     _sourceCulture;
        vector<string>             _listCulture;
        vector<string>             _listTablePath;
        vector<LocalizationRecord> _listRecord;
        vector<string>             _listRemovedKey; ///< 표에서 지운 키 — 저장이 원문 표 · 번역 표에서 지운다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 게임 데이터 XML 파일 항목 */
    struct GameDataFileEntry
    {
        string _fileName;
        string _absolutePath;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorDataTableCommands
     * @brief Data Table 패널의 파일 IO를 ImGui 없이 수행합니다.
     */
    class EditorDataTableCommands
    {
    public:
        /** @brief 편집할 수 있는 로컬라이제이션 프로젝트(엔진 · 활성 게임 팩의 `data/localization` 의 프로젝트 파일)의 절대 경로입니다. */
        static void collectLocalizationProjects( vector<string>& outListProjectPath );
        /**
         * @brief 프로젝트 하나를 표로 읽습니다. 번역 표가 없으면 그 문화권은 빈 칸이고, 있는데 못 읽으면 경고하고 false 입니다(읽은 데까지 채운다).
         */
        [[nodiscard]] static bool loadLocalizationProject( string_view projectPath, LocalizationSheet& outSheet );
        /**
         * @brief 고친 줄을 원문 표 · 번역 표에 씁니다. 번역을 고친 칸은 지금 원문의 해시를 받고 검토 표시가 풀립니다 — 원문만 고친 줄의 번역은 낡은 것이 됩니다.
         * @details **읽지 못한 기존 파일은 덮지 않습니다** — 하나라도 못 썼으면 false 이고 고친 표시가 남습니다(깨진 번역 표를 덮으면 그 문화권의 번역이 모두 지워진다).
         *          다 쓰면 실행 중인 `LocalizationManager` 가 그 프로젝트를 다시 읽습니다(핫 리로드).
         */
        [[nodiscard]] static bool saveLocalizationProject( LocalizationSheet& inoutSheet );
        /** @brief data 폴더의 XML 파일 목록을 채웁니다. */
        static bool collectGameDataFiles( vector<GameDataFileEntry>& outList );

        /** @brief 활성 게임의 Resource/.../data/localization 폴더 절대 경로를 반환합니다. */
        static string getLocalizationFolderPath();
        /** @brief Resource/.../data 폴더 절대 경로를 반환합니다. */
        static string getGameDataFolderPath();
    };
} // namespace sw::editor
