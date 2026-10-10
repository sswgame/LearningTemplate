/**
 * @file DebugHUD.h
 * @brief 런타임 디버그 오버레이 HUD 입니다(Dev 전용 — Shipping 에는 없다). 게임 창 · 에디터 게임 뷰 · 모든 Dev 실행에서 런타임 UI 위에 그립니다.
 */
#pragma once
#include "Engine/Console/DebugHUDRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Common/Macros.h"
    #include "Core/Common/Types.h"
    #include "Core/String/hashed_string.h"

    #include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    class InputMap;
    class UISystem;
    class UserSettingsManager;

    /** @brief HUD 를 붙이는 화면 모서리입니다. 값은 설정 `debug.hudCorner` 의 선택지 순서와 `gv_debugHUDCorner` 입니다. */
    enum class DebugHUDCorner : uint8
    {
        TopLeft = 0,
        TopRight,
        BottomLeft,
        BottomRight,
        Count
    };
} // namespace sw

namespace sw
{
    /**
     * @class DebugHUD
     * @brief 디버그 HUD 의 조종자입니다 — 마스터 단축키 · 켜짐 상태 · HUD 화면과 설정 창을 엽니다(언리얼 `stat` · Godot 디버그 모니터 자리).
     * @details 상태는 사용자 설정(`engine/settings/debughud.settings.xml` — `debug.hud` · `debug.hudSections` · `debug.hudCorner` · `debug.hudOpacity`)이고
     *          전역 변수(`gv_debugHUD*`)가 그 대상입니다. 쓰기는 설정 매니저로 가서 사용자 파일에 남습니다(스키마가 없으면 전역 변수에 바로).
     *
     *          - HUD 화면은 오버레이 층이라 포커스 · 포인터 · 게임 입력을 받지 않습니다. 켜진 섹션만 4 Hz 로 본문을 부르고 줄을 다시 씁니다.
     *          - 설정 창(`engine/ui/debughud.ui.xml`)은 모달이라 열린 동안만 UI 가 입력을 가집니다(`UISystem::isGameInputBlocked`).
     *          - 꺼져 있으면 프레임 비용은 셸 맵 액션 하나를 읽는 것뿐입니다(할당 없음).
     *          - `kUsesFrameProfiler` 섹션이 보이는 동안 `FrameProfiler` 를 켜고, HUD 가 켠 것이면 감출 때 끕니다.
     *
     *          게임 스레드에서만 씁니다. `EngineLoop` 가 UI 기동 단계에서 만들고 `UISystem::update` 앞에서 `update` 를 부릅니다.
     */
    class SW_API DebugHUD
    {
    public:
        /** @brief Dev 기동이 엔진 · 게임 스키마 뒤에 덧붙이는 설정 스키마입니다. */
        static constexpr const utf8* kSettingsSchemaPath = "engine/settings/debughud.settings.xml";
        /** @brief 섹션 체크 상자 창 문서입니다. */
        static constexpr const utf8* kWindowDocumentPath = "engine/ui/debughud.ui.xml";
        /** @brief 셸 입력 맵(`engine/input/default.input.xml`) Debug 레이어의 마스터 단축키 액션입니다(Ctrl+F3). */
        static constexpr const utf8* kToggleActionName = "DebugHUDToggle";
        /** @brief HUD 패널 위젯 이름입니다(레이아웃 덤프 · 탐침이 찾는다). */
        static constexpr const utf8* kPanelName = "DebugHUDPanel";
        /** @brief 불투명도 범위입니다(설정 정의와 같다 — 0 이면 HUD 를 켜고도 보이지 않는다). */
        static constexpr float32 kMinOpacity = 0.2f;
        static constexpr float32 kMaxOpacity = 1.0f;

        DebugHUD();
        ~DebugHUD();
        DebugHUD( const DebugHUD& )            = delete;
        DebugHUD& operator=( const DebugHUD& ) = delete;

        /**
         * @brief UI 시스템과 설정 출처를 묶고, 명령 · 탐침이 찾는 조종자로 둡니다.
         * @param pSettings 설정 매니저(nullptr 이거나 HUD 스키마를 읽지 않았으면 전역 변수에 바로 쓴다 — 시험).
         */
        void initialize( UISystem& ui, UserSettingsManager* pSettings );
        /** @brief 화면 · 창을 닫고 HUD 가 켠 프로파일러를 끕니다. */
        void shutdown();
        /**
         * @brief 프레임마다(`UISystem::update` 앞) — 마스터 단축키 → 켜짐 상태 → HUD 화면 열기 · 닫기 → 프로파일러 → 닫힌 창의 설정 확정.
         * @param pShellMap 셸 입력 맵(없으면 단축키를 보지 않는다).
         */
        void update( const InputMap* pShellMap );

        /** @brief HUD 가 켜져 있으면 true 입니다(`gv_debugHUD`). */
        bool isShown() const;
        /** @brief HUD 를 켜거나 끕니다(설정 `debug.hud` — 저장된다). 화면은 다음 `update` 가 연다. */
        void setShown( bool bShown );
        /** @brief 섹션이 켜져 있으면 true 입니다(설정 글 + 등록 기본). */
        static bool isSectionShown( const DebugHUDSectionRegistration& registration );
        /** @brief 섹션을 켜거나 끕니다(설정 `debug.hudSections`). 등록되지 않은 이름이면 false 입니다. */
        [[nodiscard]] bool setSectionShown( string_view name, bool bShown );
        /** @brief 등록된 섹션 중 켜진 것의 수입니다(HUD 가 꺼져 있어도 센다). */
        static uint32         getShownSectionCount();
        static DebugHUDCorner getCorner();
        /** @brief 모서리를 바꿉니다(설정 `debug.hudCorner`). */
        void           setCorner( DebugHUDCorner corner );
        static float32 getOpacity();
        /** @brief 불투명도를 바꿉니다(설정 `debug.hudOpacity` — 0.2..1 로 묶는다). */
        void setOpacity( float32 opacity );

        /** @brief 설정 창을 엽니다(이미 열렸으면 그대로). 열지 못하면(문서 오류) false 입니다. */
        [[nodiscard]] bool openWindow();
        void               closeWindow();
        bool               isWindowOpen() const;
        /** @brief HUD 화면이 열려 있으면 true 입니다(켜짐과 다음 `update` 사이에는 다를 수 있다). */
        bool isScreenOpen() const;
        /** @brief HUD 화면이 지금 그리는 섹션 수입니다(화면이 없으면 0 — 탐침 `DebugHUD.SectionCount`). */
        uint32 getDrawnSectionCount() const;
        /** @brief HUD 화면이 섹션 @p name 을 그리고 있으면 true 입니다(탐침 `DebugHUD.SectionShown`). */
        bool isSectionDrawn( string_view name ) const;
        /**
         * @brief HUD 패널이 실제로 놓인 모서리(지난 레이아웃 결과 — 패널 가운데가 뷰포트의 어느 4 분면인가)입니다.
         * @return 화면이 없거나 아직 배치 전이면 false 입니다.
         */
        [[nodiscard]] bool tryGetArrangedCorner( DebugHUDCorner& outCorner ) const;

        /** @brief 지금 동작 중인 조종자입니다(개발 명령 `hud` · 자동화 탐침이 찾는다). 없으면 nullptr 입니다. */
        static DebugHUD* findActive();
        /** @brief `topLeft` · `topright` · `0`..`3` 을 모서리로 읽습니다(대소문자 무시). */
        [[nodiscard]] static bool tryParseCorner( string_view text, DebugHUDCorner& outCorner );
        /** @brief 설정 선택지 이름(`topLeft` …)입니다. */
        static const utf8* getCornerName( DebugHUDCorner corner );

    private:
        /** @brief 설정 @p settingID 에 @p value 를 쓰고 확정합니다. 스키마가 없으면 @p pVariableName 전역 변수에 바로 씁니다. */
        void writeSetting( const hashed_string& settingID, const utf8* pVariableName, string_view value );
        /** @brief 켜짐 상태와 HUD 화면을 맞춥니다. */
        void syncScreen();
        /** @brief 보이는 섹션이 프로파일러를 쓰면 켜고, HUD 가 켠 것을 더 쓰지 않으면 끕니다. */
        void syncFrameProfiler();

    private:
        UISystem*              _pUI;
        UserSettingsManager*   _pSettings;
        hashed_string          _toggleAction;
        UIScreenHandle         _screen;                    ///< HUD 화면(오버레이 층 — 없으면 무효)
        UIScreenHandle         _window;                    ///< 설정 창(모달 — 없으면 무효)
        uint8                  _bProfilerEnabledByHUD : 1; ///< 프로파일러를 HUD 가 켰다(감출 때 끈다)
        [[maybe_unused]] uint8 _reserved              : 7;
    };
} // namespace sw

#endif
