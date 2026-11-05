/**
 * @file EditorThemeConfigUtil.h
 * @brief 테마 값 다루기입니다 — 이름으로 벌 찾기, 사용자 덮어쓰기(강조색 · 둥글기), 환경설정 값과의 왕복(ImGui 없음). 스타일에 쓰는 일은 `EditorThemeUtil` 이 합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Config/EditorThemeConfig.h"

namespace sw::editor
{
    struct EditorConfig;

    /**
     * @struct EditorThemeConfigUtil
     * @brief `EditorThemeConfig` 의 값 규칙입니다. 덮어쓴 값이 있으면 그 표시(`_bAccentOverridden` · `_bRadiusOverridden`)와 토큰 값을 함께 바꿉니다.
     */
    struct EditorThemeConfigUtil
    {
        /** @brief 이름(대소문자 무시)이 같은 벌입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static const EditorThemeConfig* findByName( const vector<EditorThemeConfig>& listTheme, string_view name );

        /**
         * @brief 환경설정(@p saved)이 고른 벌에 저장된 덮어쓰기를 얹어 @p outTheme 에 씁니다. 그 이름의 벌이 없으면 false 이고 @p outTheme 는 그대로입니다 — 다른 벌로 대신하지 않습니다.
         * @details 강조색은 세 채널이 모두 있어야 덮어쓴다. 둥글기는 슬라이더마다 따로 본다 — 음수인 칸은 토큰 값을 쓴다.
         */
        [[nodiscard]] static bool buildFromSaved( const vector<EditorThemeConfig>& listTheme, const EditorConfig& saved, EditorThemeConfig& outTheme );

        /** @brief 강조색을 덮어씁니다. */
        static void overrideAccent( const Color& accent, EditorThemeConfig& inoutTheme );

        /**
         * @brief 둥글기 슬라이더 셋을 덮어씁니다. 팝업은 창, 스크롤바 · 그랩은 입력 필드를 따릅니다.
         * @details 음수는 0 으로 묶는다. 슬라이더에 손으로 친 음수를 그대로 두면 저장 값의 "덮어쓰지 않음" 과 같아져 다음 기동에 사라진다.
         */
        static void overrideRadius( float32 window, float32 frame, float32 tab, EditorThemeConfig& inoutTheme );

        /** @brief @p theme 의 이름과 덮어쓴 값을 환경설정 칸에 씁니다. 덮어쓰지 않은 값은 `EditorConfig::kNotOverridden` 이라, 토큰 파일이 바뀌면 그 값을 따릅니다. */
        static void writeSaved( const EditorThemeConfig& theme, EditorConfig& outSaved );
    };
} // namespace sw::editor
