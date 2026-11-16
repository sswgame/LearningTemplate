/**
 * @file EditorDataTableCommands.h
 * @brief 로컬라이제이션 프로젝트(원문 테이블과 문화권별 번역 테이블)와 게임 데이터 XML 파일을 읽고 쓰는 커맨드입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorLocalizationSheet.h"

namespace sw::editor
{
    /**
     * @class EditorDataTableCommands
     * @brief Data Table 패널의 파일 IO를 ImGui 없이 수행합니다.
     */
    class EditorDataTableCommands
    {
    public:
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
        /** @brief `saveLocalizationProject` 가 쓸 수 있는 파일(원문 테이블 · 문화권마다 번역 테이블, 디스크 경로)을 @p outListPath 에 더합니다. 쓰기 전 읽기 전용 확인이 본다. */
        static void collectLocalizationFiles( const LocalizationSheet& sheet, vector<string>& outListPath );
        /** @brief @p dataFolder(`getGameDataFolderPath`)의 XML 파일 목록을 채웁니다. 에디터 상태를 읽지 않아 워커에서 불러도 된다. 폴더가 없거나 목록이 도중에 멈췄으면 false 입니다(`FileUtil::collectFiles` 와 같다). */
        [[nodiscard]] static bool collectGameDataFiles( string_view dataFolder, vector<GameDataFileEntry>& outList );

        /** @brief Content/.../data 폴더 절대 경로를 반환합니다. */
        static string getGameDataFolderPath();
    };
} // namespace sw::editor
