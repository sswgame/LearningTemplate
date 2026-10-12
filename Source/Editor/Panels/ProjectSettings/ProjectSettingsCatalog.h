/**
 * @file ProjectSettingsCatalog.h
 * @brief 프로젝트 설정 창이 고칠 수 있는 설정 파일의 목록입니다. 생성 메타데이터 `docs/Config/ConfigReference.json` 에서 읽습니다(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /** @brief 설정 파일 하나입니다. 같은 타입이 파일 여럿이면(`Config/Game` 아래 게임마다 하나) 파일마다 하나씩 나옵니다. */
    struct ProjectSettingsEntry
    {
        string _page;         ///< 목록에 보이는 이름. 설정 색인의 페이지 이름이고, 와일드카드 파일은 `GameConfig/NileCity` 처럼 파일 이름이 붙는다
        string _relativePath; ///< 프로젝트 루트 기준 경로(슬래시). `expandWildcards` 전에는 `*` 가 들어 있을 수 있다
        string _typeName;     ///< 리플렉션 타입 이름(`EngineConfig`)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ProjectSettingsCatalog
     * @brief 설정 색인(`ConfigReference.json`)에서 창이 고칠 수 있는 JSON 설정 파일을 고릅니다.
     * @details 언리얼 Project Settings · 유니티 Project Settings 가 설정 종류마다 페이지를 두는 것과 같습니다. 이 저장소의 설정 종류와 위치는
     *          생성 문서 한 곳(`GenerateConfigReference.py` 가 코드의 `REFLECT` 구조체에서 만든다)이 알고 있어, 창은 목록을 따로 들지 않는다.
     */
    struct ProjectSettingsCatalog
    {
        /**
         * @brief 색인 JSON 에서 고칠 수 있는 항목을 모읍니다(먼저 비운다). JSON 이 아니면 false 입니다.
         * @details 고르는 규칙: 경로가 `Config/` 나 `Resource/` 아래의 `.json` 이고, 타입 이름이 있는 것. 모듈 매니페스트(`Source/`)와 XML 설정은 뺀다 —
         *          매니페스트는 Modules 창이, XML 은 손으로 쓴 주석이 있어 다시 쓰면 잃기 때문이다.
         */
        [[nodiscard]] static bool parseReference( string_view referenceJSON, vector<ProjectSettingsEntry>& outListEntry );

        /**
         * @brief 경로에 `*` 가 든 항목을 @p projectRoot 아래의 실제 파일로 풀어 파일마다 항목 하나로 바꿉니다. 파일이 없으면 그 항목은 사라집니다.
         * @details 풀린 항목의 `_page` 는 `<페이지>/<확장자 없는 파일 이름>` 입니다. 순서는 파일 이름 사전순입니다.
         */
        static void expandWildcards( string_view projectRoot, vector<ProjectSettingsEntry>& inoutListEntry );

        /** @brief 경로가 `Config/` 나 `Resource/` 아래의 JSON 이면 true 입니다. */
        [[nodiscard]] static bool isEditableLocation( string_view relativePath );
    };
} // namespace sw::editor
