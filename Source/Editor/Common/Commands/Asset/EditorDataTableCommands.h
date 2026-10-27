/**
 * @file EditorDataTableCommands.h
 * @brief 로컬라이제이션 프로젝트(원문 테이블과 문화권별 번역 테이블)와 게임 데이터 XML 파일을 읽고 쓰는 커맨드입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Localization/LocalizationDocuments.h"

namespace sw::editor
{
    /** @brief 테이블의 한 줄입니다. 키 하나의 원문과 메타데이터, 문화권마다의 번역을 가지고 있습니다. */
    struct LocalizationRecord
    {
        string                   _key;
        string                   _source;
        string                   _context;
        string                   _comment;
        vector<string>           _listTranslation; ///< `LocalizationSheet::_listCulture` 와 같은 순서
        vector<TranslationState> _listState;       ///< 읽었을 때의 상태(낡음, 검토, 없음)
        uint32                   _maxLength{ 0 };
        uint32                   _tableIndex{ 0 }; ///< 이 키가 있는 원문 테이블(`LocalizationSheet::_listTablePath`)
        bool                     _bModified{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로젝트 하나를 테이블로 펼친 것입니다. */
    struct LocalizationSheet
    {
        string                     _projectPath;
        string                     _sourceCulture;
        vector<string>             _listCulture;
        vector<string>             _listTablePath;
        vector<LocalizationRecord> _listRecord;
        vector<string>             _listRemovedKey; ///< 테이블에서 지운 키. 저장할 때 원문 테이블과 번역 테이블에서 지운다
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
        /** @brief 편집할 수 있는 로컬라이제이션 프로젝트의 절대 경로입니다. 엔진과 활성 게임 팩의 `data/localization` 에 있는 프로젝트 파일입니다. */
        static void collectLocalizationProjects( vector<string>& outListProjectPath );
        /**
         * @brief 프로젝트 하나를 테이블로 읽습니다. 번역 테이블이 없으면 그 문화권은 빈 항목이고, 있는데 못 읽으면 읽은 데까지 채운 뒤 경고하고 false 입니다.
         */
        [[nodiscard]] static bool loadLocalizationProject( string_view projectPath, LocalizationSheet& outSheet );
        /**
         * @brief 고친 줄을 원문 테이블과 번역 테이블에 씁니다. 번역을 고친 항목은 지금 원문의 해시를 받고 검토 표시가 해제됩니다. 원문만 고친 줄의 번역은 낡은 것이 됩니다.
         * @details **읽지 못한 기존 파일은 덮어쓰지 않는다.** 깨진 번역 테이블을 덮어쓰면 그 문화권의 번역이 모두 지워지기 때문이다.
         *          하나라도 못 썼으면 false 이고 고친 표시가 남는다.
         *          다 쓰면 실행 중인 `LocalizationManager` 가 그 프로젝트를 다시 읽습니다(핫 리로드).
         */
        [[nodiscard]] static bool saveLocalizationProject( LocalizationSheet& inoutSheet );
        /** @brief @p dataFolder(`getGameDataFolderPath`)의 XML 파일 목록을 채웁니다. 에디터 상태를 읽지 않아 워커에서 불러도 된다. 폴더가 없거나 목록이 도중에 멈췄으면 false 입니다(`FileUtil::collectFiles` 와 같다). */
        [[nodiscard]] static bool collectGameDataFiles( string_view dataFolder, vector<GameDataFileEntry>& outList );

        /** @brief 활성 게임의 Resource/.../data/localization 폴더 절대 경로를 반환합니다. */
        static string getLocalizationFolderPath();
        /** @brief Resource/.../data 폴더 절대 경로를 반환합니다. */
        static string getGameDataFolderPath();
    };
} // namespace sw::editor
