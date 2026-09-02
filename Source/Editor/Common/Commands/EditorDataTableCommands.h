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
    struct LocRecord
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
        string _relativePath;
        string _absolutePath;
    };

    /**
     * @class EditorDataTableCommands
     * @brief Data Table 패널이 쓰던 파일 IO를 ImGui 없이 수행합니다.
     */
    class EditorDataTableCommands
    {
    public:
        /** @brief ko/en/ja JSON을 읽어 레코드 목록을 만듭니다. */
        static bool loadLocalization( vector<LocRecord>& outList );
        /** @brief 레코드를 언어별 JSON으로 저장하고 LocalizationManager를 갱신합니다. */
        static bool saveLocalization( vector<LocRecord>& listRecord );
        /** @brief data 폴더의 XML 파일 목록을 채웁니다. */
        static bool collectGameDataFiles( vector<GameDataFileEntry>& outList );

        /** @brief Resource/.../localization 폴더 절대 경로를 반환합니다. */
        static string getLocalizationFolderPath();
        /** @brief Resource/.../data 폴더 절대 경로를 반환합니다. */
        static string getGameDataFolderPath();
        /** @brief 수정된 로컬라이즈 레코드가 있으면 true입니다. */
        static bool hasModifiedLocalization( const vector<LocRecord>& listRecord );
    };
} // namespace sw::editor
