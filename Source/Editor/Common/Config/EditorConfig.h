/**
 * @file EditorConfig.h
 * @brief **앱이 다시 쓰는** 에디터 상태 (Config/Editor/EditorConfig.json).
 *
 * @details 여기 있는 값은 에디터가 `saveToHost()` 로 **파일 전체를 다시 생성**하며 덮어쓴다
 *          (테마 대화상자의 저장). 그래서 손으로 적은 것 — 주석·순서·손으로 고른 목록 —
 *          은 여기 두면 안 된다. 그런 설정은 읽기 전용인 `EditorData`(editordata.json) 에 있다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /**
     * @brief 에디터가 저장하는 상태 (Shipping 미포함)
     * @details 지금은 테마뿐이다. **필드를 늘리기 전에** 그 값을 앱이 쓰는지 사람이 쓰는지 보라 —
     *          사람이 쓰는 것은 `EditorData` 다.
     */
    REFLECT()
    struct EditorConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _themePreset{ "ModernDark" };

        PROPERTY()
        float32 _themeAccentR{ 0.27f };

        PROPERTY()
        float32 _themeAccentG{ 0.57f };

        PROPERTY()
        float32 _themeAccentB{ 1.0f };

        PROPERTY()
        float32 _themeWindowRounding{ 4.0f };

        PROPERTY()
        float32 _themeFrameRounding{ 3.0f };

        PROPERTY()
        float32 _themeTabRounding{ 4.0f };

        static void                setActive( const EditorConfig& config );
        static const EditorConfig& getActive();
        /** @brief Host JSON을 읽어 active 설정을 채웁니다. 파일이 없으면 cpp 기본값입니다. */
        static void loadFromHost();
        /** @brief active 설정을 Host JSON 파일에 저장합니다. */
        static void saveToHost();
    };
} // namespace sw::editor
