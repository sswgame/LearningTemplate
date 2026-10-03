/**
 * @file EditorDataTableCommands.h
 * @brief 로컬라이즈 JSON / 게임 데이터 XML 파일 IO 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /** @brief 로컬라이제이션 문자열 다국어 레코드 */
    struct LocalizationRecord
    {
        string _key;
        string _enUS;
        string _koKR;
        string _jaJP;
        bool   _bModified{ false };
    };

    /** @brief 게임 데이터 XML 파일 항목 */
    struct GameDataFileEntry
    {
        string _fileName;
        string _absolutePath;
    };

    /**
     * @class EditorDataTableCommands
     * @brief Data Table 패널이 쓰던 파일 IO를 ImGui 없이 수행합니다.
     */
    class EditorDataTableCommands
    {
    public:
        /** @brief 활성 게임의 ko/en/ja JSON을 읽어 레코드 목록을 만듭니다(`loadLocalizationFrom`). 읽지 못한 언어 파일이 있으면 false 입니다. */
        [[nodiscard]] static bool loadLocalization( vector<LocalizationRecord>& outList );
        /** @brief 레코드를 활성 게임의 언어별 JSON으로 저장하고 LocalizationManager를 갱신합니다(`saveLocalizationTo`). */
        [[nodiscard]] static bool saveLocalization( vector<LocalizationRecord>& listRecord );
        /** @brief 폴더 하나의 ko/en/ja JSON을 읽습니다. 파일이 없으면 그 언어는 빈 칸이고, 있는데 못 읽으면 경고하고 false 입니다. */
        [[nodiscard]] static bool loadLocalizationFrom( string_view localizationFolder, vector<LocalizationRecord>& outList );
        /**
         * @brief 폴더 하나에 언어별 JSON을 씁니다. **읽지 못한 기존 파일은 덮지 않습니다** — 하나라도 못 썼으면 false 이고 고친 표시가 남습니다.
         * @details 예전에는 깨진 언어 파일을 읽을 때 건너뛰어 그 언어 칸이 비었고, 저장이 빈 칸으로 그 파일을 다시 써 **번역을 모두 지웠다.**
         */
        [[nodiscard]] static bool saveLocalizationTo( string_view localizationFolder, vector<LocalizationRecord>& listRecord );
        /** @brief data 폴더의 XML 파일 목록을 채웁니다. */
        static bool collectGameDataFiles( vector<GameDataFileEntry>& outList );

        /** @brief Resource/.../localization 폴더 절대 경로를 반환합니다. */
        static string getLocalizationFolderPath();
        /** @brief Resource/.../data 폴더 절대 경로를 반환합니다. */
        static string getGameDataFolderPath();
    };
} // namespace sw::editor
