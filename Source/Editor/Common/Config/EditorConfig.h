/**
 * @file EditorConfig.h
 * @brief 에디터 테마입니다. 환경설정 섹션 "Editor/Appearance" 로 등록되어 `Saved/Editor/EditorPreferences.json` 에 기본과 다른 값만 저장됩니다.
 *
 * @details 저장 파일은 앱이 통째로 다시 씁니다. 손으로 적은 것(주석 · 순서)은 남지 않으므로, 사람이 커밋하는 설정은
 *          `EditorToolDefaults`(editortooldefaults.json)에 둡니다.
 */
#pragma once
#include "Core/Container/string.h"

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

        PROPERTY()
        string _themePreset{ "ModernDark" }; ///< 테마 프리셋 이름(테마 대화 상자의 목록)

        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _themeAccentR{ 0.27f }; ///< 강조색 R(0..1)

        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _themeAccentG{ 0.57f }; ///< 강조색 G(0..1)

        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _themeAccentB{ 1.0f }; ///< 강조색 B(0..1)

        PROPERTY( Min = 0.0 )
        float32 _themeWindowRounding{ 4.0f }; ///< 창 모서리 둥글기(픽셀)

        PROPERTY( Min = 0.0 )
        float32 _themeFrameRounding{ 3.0f }; ///< 입력 필드와 버튼의 모서리 둥글기(픽셀)

        PROPERTY( Min = 0.0 )
        float32 _themeTabRounding{ 4.0f }; ///< 탭 모서리 둥글기(픽셀)

        static void                setActive( const EditorConfig& config );
        static const EditorConfig& getActive();
        /** @brief 활성 설정의 주소입니다(환경설정 섹션 Appearance 가 이 인스턴스를 그린다). */
        static EditorConfig* getActiveInstance();
        /** @brief 지금 테마를 환경설정 파일에 저장합니다(`EditorPreferencesStore::saveAll`). 테마 대화상자가 부릅니다. */
        static void saveToPreferences();
    };
} // namespace sw::editor
