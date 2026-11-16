/**
 * @file ProjectSettingsEntry.h
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
        string _relativePath; ///< 프로젝트 루트 기준 경로(슬래시) - 보여 주기용
        string _absolutePath; ///< 설정 테이블이 읽은 자리(절대, 슬래시). 창은 이것을 열고 쓴다 - 상대 경로에서 다시 풀지 않는다(R8)
        string _typeName;     ///< 리플렉션 타입 이름(`EngineConfig`). 비면 표 페이지다(`collectTables`)
        string _elementName;  ///< 비면 파일 전체가 설정(JSON)이다. 있으면 XML 파일 안의 그 요소 하나가 설정이다(팩 `gamesettings.xml` 의 `<Engine>`)

        // 설정 테이블이 파일에서 읽은 설정 가운데 창이 고칠 수 있는 JSON 설정 파일을 고릅니다.
        // 언리얼 Project Settings · 유니티 Project Settings 가 설정 종류마다 페이지를 두는 것과 같습니다. 목록은 로드된 설정이 곧 정본이다 —
        /**
         * @brief @p configManager 가 파일에서 읽은 설정 중 고칠 수 있는 것을 모읍니다(먼저 비운다). 순서는 페이지 이름 사전순입니다.
         * @details 고르는 규칙: @p projectRoot 아래 `Config/` 나 `Content/` 의 `.json` 이다. 모듈 매니페스트(`Source/`)와 XML 설정은 뺀다 —
         *          매니페스트는 Modules 창이, XML 은 손으로 쓴 주석이 있어 다시 쓰면 잃기 때문이다.
         */
        static void collectLoaded( const ConfigManager& configManager, string_view projectRoot, vector<ProjectSettingsEntry>& outListEntry );

        /** @brief 표 페이지를 훑는 폴더(프로젝트 루트 기준)입니다. */
        static constexpr const utf8* kTableFolder = "Config";

        /**
         * @brief @p projectRoot 의 `Config/` 아래 모든 JSON 가운데 @p listTyped(반사 형식 페이지)가 들지 않은 파일을 표 페이지로 모읍니다(먼저 비운다). 순서는 경로 사전순입니다.
         * @details 린트 · 격리 · 환경 표와 C++ 가 직접 읽는 개발 표처럼 에디터가 형식으로 읽지 않는 설정도 고칠 수 있게 한다 — `_typeName` 이 비어 범용 JSON 페이지(`ProjectSettingsJSONDocument`)로 연다
         *          (Godot ProjectSettings 가 형식 없는 값을 칸으로 고치는 자리). 도구가 만든 `*.generated.json` 도 목록에 들되 읽기 전용이다.
         * @return 폴더를 훑지 못하면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool collectTables( string_view projectRoot, const vector<ProjectSettingsEntry>& listTyped, vector<ProjectSettingsEntry>& outListEntry );
        /** @brief 반사 형식 없이 범용 JSON 페이지로 여는 표면 true 입니다. */
        [[nodiscard]] bool isTable() const { return _typeName.empty(); }

        /** @brief 경로가 `Config/` 나 `Content/` 아래의 JSON 이면 true 입니다. */
        [[nodiscard]] static bool isEditableLocation( string_view relativePath );
    };
} // namespace sw::editor
