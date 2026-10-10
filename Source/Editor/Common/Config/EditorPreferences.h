/**
 * @file EditorPreferences.h
 * @brief 에디터 환경설정의 기본 섹션입니다(General, Viewport, Play, Content Browser). 테마(Appearance)는 `EditorConfig` 입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief 일반 — 시작 · 외부 도구. */
    REFLECT()
    struct EditorGeneralPreferences
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Scene opened when the editor starts (empty: the game's first scene). -gv_editorStartupScene wins when given" )
        string _startupScene{};

        PROPERTY( Tooltip = "Command that opens a log line in an IDE ({file}, {line}). Empty: VS Code (code -g)" )
        string _ideOpenCommand{};

        PROPERTY( Min = 0.0, Max = 3.0, Tooltip = "Editor UI scale. 0 follows the monitor DPI. -gv_editorUiScale wins when given" )
        float32 _uiScale{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 뷰포트 — 새 뷰포트의 기본값입니다(이미 연 뷰포트는 툴바에서 바꿉니다). */
    REFLECT()
    struct EditorViewportPreferences
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.1, Max = 100.0, Tooltip = "Editor camera fly speed (m/s)" )
        float32 _cameraSpeed{ 5.0f };

        PROPERTY( Min = 0.01, Tooltip = "Translate snap step (m)" )
        float32 _gridSnapValue{ 1.0f };

        PROPERTY( Min = 1.0, Max = 90.0, Tooltip = "Rotate snap step (deg)" )
        float32 _rotationSnapValue{ 15.0f };

        PROPERTY( Min = 0.01, Tooltip = "Scale snap step" )
        float32 _scaleSnapValue{ 0.1f };

        PROPERTY( Tooltip = "Show the frame statistics overlay" )
        bool _bShowStats{ true };

        PROPERTY( Tooltip = "Show the viewport grid" )
        bool _bShowGrid{ true };

        PROPERTY( Tooltip = "Show the orientation cube" )
        bool _bShowOrientationCube{ true };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 플레이 — 플레이 옵션(메뉴 Play · 게임 뷰 툴바)입니다. */
    REFLECT()
    struct EditorPlayPreferences
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Maximize the game view while playing (restored on Stop). Simulate keeps the layout" )
        bool _bMaximizeOnPlay{ false };

        PROPERTY( Tooltip = "Extra command line for Play in New Window (for example -dx12)" )
        string _standaloneArguments{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 콘텐츠 브라우저 — 창을 열 때의 기본값입니다(연 창은 툴바에서 바꿉니다). */
    REFLECT()
    struct EditorContentBrowserPreferences
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Show every game pack under game/ instead of the active one only" )
        bool _bShowAllPacksByDefault{ false };
    };
} // namespace sw::editor
