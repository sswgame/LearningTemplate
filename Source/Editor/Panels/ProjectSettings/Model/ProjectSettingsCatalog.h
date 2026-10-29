/**
 * @file ProjectSettingsCatalog.h
 * @brief 프로젝트 설정 창이 고칠 수 있는 설정 파일의 목록입니다. 설정 테이블(`ConfigManager`)이 파일에서 읽은 설정에서 만듭니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class ConfigManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 설정 파일 하나입니다. 같은 타입이 파일 여럿이면(`Config/Game` 아래 게임마다 하나) 파일마다 하나씩 나옵니다. */
    struct ProjectSettingsEntry
    {
        string _page;         ///< 목록에 보이는 이름(리플렉션 타입 이름)
        string _relativePath; ///< 프로젝트 루트 기준 경로(슬래시)
        string _typeName;     ///< 리플렉션 타입 이름(`EngineConfig`)
        string _elementName;  ///< 비면 파일 전체가 설정(JSON)이다. 있으면 XML 파일 안의 그 요소 하나가 설정이다(팩 `gamesettings.xml` 의 `<Engine>`)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ProjectSettingsCatalog
     * @brief 설정 테이블이 파일에서 읽은 설정 가운데 창이 고칠 수 있는 JSON 설정 파일을 고릅니다.
     * @details 언리얼 Project Settings · 유니티 Project Settings 가 설정 종류마다 페이지를 두는 것과 같습니다. 목록은 로드된 설정이 곧 정본이다 —
     *          생성 문서를 따로 두면 생성기가 사라지거나 늦을 때 창이 빈다(옛 `ConfigReference.json` 이 그랬다).
     */
    struct ProjectSettingsCatalog
    {
        /**
         * @brief @p configManager 가 파일에서 읽은 설정 중 고칠 수 있는 것을 모읍니다(먼저 비운다). 순서는 페이지 이름 사전순입니다.
         * @details 고르는 규칙: @p projectRoot 아래 `Config/` 나 `Content/` 의 `.json` 이다. 모듈 매니페스트(`Source/`)와 XML 설정은 뺀다 —
         *          매니페스트는 Modules 창이, XML 은 손으로 쓴 주석이 있어 다시 쓰면 잃기 때문이다.
         */
        static void collectLoaded( const ConfigManager& configManager, string_view projectRoot, vector<ProjectSettingsEntry>& outListEntry );

        /** @brief 경로가 `Config/` 나 `Content/` 아래의 JSON 이면 true 입니다. */
        [[nodiscard]] static bool isEditableLocation( string_view relativePath );
    };
} // namespace sw::editor
