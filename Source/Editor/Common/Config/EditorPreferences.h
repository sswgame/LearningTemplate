/**
 * @file EditorPreferences.h
 * @brief 에디터 환경설정 섹션의 값 형식입니다(General, Viewport, Play, Content Browser, Multiplayer Sync). 테마(Appearance)는 `EditorConfig` 입니다.
 * @details 기본 섹션의 등록은 `EditorPreferences.cpp`, Multiplayer Sync 의 등록은 그 탭(`MultiplayerSyncTab.cpp`)이 `SW_EDITOR_SETTINGS` 로 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

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

        PROPERTY( Min = 0.0174533, Max = 1.5707964, Units = rad, Tooltip = "Rotate snap step" )
        float32 _rotationSnapValue{ MathUtil::toRadian( 15.0f ) };

        PROPERTY( Min = 0.01, Tooltip = "Scale snap step" )
        float32 _scaleSnapValue{ 0.1f };

        // 기본은 끈다. 첫 화면에서 HUD 상자가 씬을 가리지 않게 한다(언리얼 · 유니티 · Godot 의 뷰포트 통계도 기본은 꺼져 있다).
        PROPERTY( Tooltip = "Show the Debug HUD box (the same sections as the game HUD) in new scene views" )
        bool _bShowHUD{ false };

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

namespace sw::editor
{
    /** @brief 멀티플레이 창 Sync 탭 — 클라이언트에게 물은 질의의 기다림 상한입니다. */
    REFLECT()
    struct EditorSyncPreferences
    {
        REFLECT_BODY();

        PROPERTY( Min = 1.0, Tooltip = "Seconds the Sync tab waits for a client's answer before it shows a timeout error (a client answers an invalid question with an error line, which is never counted as an answer)" )
        float32 _clientReplyTimeoutSeconds{ 10.0f };
    };
} // namespace sw::editor
