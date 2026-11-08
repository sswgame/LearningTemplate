/**
 * @file EditorPreferences.h
 * @brief 에디터 환경설정 섹션의 값 형식입니다(General, Viewport, Play, Content Browser, Diagnostics, Multiplayer Sync). 테마(Appearance)는 `EditorAppearanceConfig` 입니다.
 * @details 기본 섹션의 등록은 `EditorPreferences.cpp`, Diagnostics 는 값을 Core 에 입히는 `EditorAssertControl.cpp`, Multiplayer Sync 의 등록은 그 탭(`MultiplayerSyncTab.cpp`)이
 *          `SW_EDITOR_SETTINGS` 로 한다.
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

        // 지평선 근처의 바닥 평면 교점이 수 km 밖에 놓이지 않게 한다. 언리얼도 커서 아래 표면이 없으면 놓지 않는다.
        PROPERTY( Min = 1.0, Max = 100000.0, Tooltip = "Farthest surface (m) an asset dragged into the scene view is placed on - beyond it nothing is placed" )
        float32 _placementMaxDistance{ 500.0f };

        // 하우징 · 건설 게임처럼 놓기 전에 돌린다(언리얼 · 유니티는 놓은 뒤 기즈모) — 기본은 직각 걸음이라 벽 · 상자를 격자에 맞춰 놓는다.
        PROPERTY( Min = 0.0174533, Max = 3.1415927, Units = rad, Tooltip = "Rotation step of the rotate keys while an asset is dragged over the scene view (before it is placed)" )
        float32 _placementRotateStep{ MathUtil::kHalfPi };
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
    /**
     * @brief 자동 저장 — 씬 사본의 주기와 남길 세대 수입니다(`EditorAutosaveCommands`). 사본은 원본을 덮지 않고 에디터 상태 폴더에만 쓴다.
     * @details 언리얼 Loading & Saving 의 Auto Save(기본 10 분)보다 짧게 둔다 — 잃어도 되는 일의 상한을 5 분으로 정했다. 세대는 무제한으로 두지 않는다.
     */
    REFLECT()
    struct EditorAutosavePreferences
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Write a copy of the scene you are editing every interval while it has unsaved changes (the original file is never overwritten)" )
        bool _bEnabled{ true };

        PROPERTY( Min = 30.0, Max = 3600.0, Units = s, Tooltip = "Seconds between autosave copies" )
        float32 _intervalSeconds{ 300.0f };

        PROPERTY( Min = 1, Max = 20, Tooltip = "Copies kept per scene - the oldest is removed when a new one is written" )
        uint32 _keepGenerations{ 3 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 진단 — 단언 대화상자(Windows Debug 대화형 실행에서 App 이 건다)를 띄울지와 "이 자리 늘 무시" 를 기억할지입니다.
     * @details 값은 `EditorAssertControl.cpp` 가 바뀔 때마다 Core 의 대화상자 설정(`internal::setAssertDialogOptions`)에 입힌다. 환경설정을 읽기 전의
     *          초기화 구간은 Core 의 기본값(둘 다 켬)으로 돈다. 자동 실행(-scenario · -unattended)은 대화상자를 걸지 않으므로 이 값과 상관없이 멈춘다.
     */
    REFLECT()
    struct EditorDiagnosticsPreferences
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Ask in a dialog when an assert fails (Windows Debug editor runs). Off: stop at the assert as automated runs do" )
        bool _bShowAssertDialog{ true };

        PROPERTY( Tooltip = "Cancel in the assert dialog ignores that file and line until the editor closes. Off: Cancel ignores it this time only" )
        bool _bRememberIgnoredAsserts{ true };
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
