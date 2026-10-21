/**
 * @file EditorConfig.h
 * @brief 에디터 테마입니다. 환경설정 섹션 "Editor/Appearance" 로 등록되어 `Saved/Editor/EditorPreferences.json` 에 기본과 다른 값만 저장됩니다.
 *        값은 등록 줄이 가리키는 인스턴스 하나이고 `getPreferences<EditorConfig>()` 로 읽습니다(따로 든 활성 사본을 두지 않는다).
 *
 * @details 저장 파일은 앱이 통째로 다시 씁니다. 손으로 적은 것(주석 · 순서)은 남지 않으므로, 사람이 커밋하는 설정은
 *          `EditorToolDefaults`(editortooldefaults.json)에 둡니다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Editor/Common/Config/EditorThemeTokens.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /**
     * @brief 에디터가 저장하는 상태입니다(Shipping 에는 없습니다).
     * @details 지금은 테마뿐입니다. **필드를 늘리기 전에** 그 값을 앱이 쓰는지 사람이 쓰는지 확인하십시오. 사람이 쓰는
     *          것은 `EditorToolDefaults` 에 둡니다.
     */
    REFLECT()
    struct EditorConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief 덮어쓰지 않았다는 표시입니다. 강조색 · 둥글기가 이 값이면 토큰 파일의 값을 쓴다. */
        static constexpr float32 kNotOverridden = -1.0f;

        PROPERTY()
        string _themeName{ "dark" }; ///< 테마 토큰 벌 이름(`Resource/editor/theme/<이름>.tokens.json`). 이 초기값이 환경설정 기본값의 유일한 자리다 — 그 벌이 없으면 에디터가 서지 않는다

        PROPERTY( Min = -1.0, Max = 1.0 )
        float32 _themeAccentR{ kNotOverridden }; ///< 덮어쓴 강조색 R(0..1, 음수면 토큰 값)

        PROPERTY( Min = -1.0, Max = 1.0 )
        float32 _themeAccentG{ kNotOverridden }; ///< 덮어쓴 강조색 G(0..1, 음수면 토큰 값)

        PROPERTY( Min = -1.0, Max = 1.0 )
        float32 _themeAccentB{ kNotOverridden }; ///< 덮어쓴 강조색 B(0..1, 음수면 토큰 값)

        PROPERTY( Min = -1.0 )
        float32 _themeWindowRounding{ kNotOverridden }; ///< 덮어쓴 창 모서리 둥글기(픽셀, 음수면 토큰 값)

        PROPERTY( Min = -1.0 )
        float32 _themeFrameRounding{ kNotOverridden }; ///< 덮어쓴 입력 필드와 버튼의 모서리 둥글기(픽셀, 음수면 토큰 값)

        PROPERTY( Min = -1.0 )
        float32 _themeTabRounding{ kNotOverridden }; ///< 덮어쓴 탭 모서리 둥글기(픽셀, 음수면 토큰 값)

        /** @brief 지금 테마를 환경설정 파일에 저장합니다(`EditorPreferencesStore::saveAll`). 테마 대화상자가 부릅니다. */
        static void saveToPreferences();
    };
} // namespace sw::editor
