/**
 * @file DebugHUD.h
 * @brief 런타임 디버그 오버레이 HUD 입니다(Dev 전용이며 Shipping 에는 없습니다). 게임 창, 에디터 게임 뷰, 모든 Dev 실행에서 런타임 UI 위에 그립니다.
 */
#pragma once
#include "Core/Console/DebugHUDRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Foundation/Macros.h"
    #include "Core/Foundation/Types.h"
    #include "Core/Container/string.h"
    #include "Core/String/hashed_string.h"

    #include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    class InputMap;
    class UISystem;
    class UserSettingsManager;

    /** @brief HUD 를 붙이는 화면 모서리입니다. 값은 설정 `debug.hudCorner` 의 선택지 순서와 `gv_debugHUDCorner` 를 따릅니다. */
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
     * @brief 디버그 HUD 를 제어하는 컨트롤러입니다. 마스터 단축키, 켜짐 상태, HUD 화면과 설정 창을 관리합니다(언리얼의 `stat`, Godot 의 디버그 모니터에 해당).
     * @details 상태는 사용자 설정(`engine/settings/debughud.settings.xml`의 `debug.hud`, `debug.hudSections`, `debug.hudCorner`, `debug.hudOpacity`)이고
     *          전역 변수(`gv_debugHUD*`)가 그 대상이다. 쓰기는 설정 매니저를 거쳐 사용자 파일에 남는다(스키마가 없으면 전역 변수에 바로 쓴다).
     *
     *          - HUD 화면은 오버레이 레이어라서 포커스, 포인터, 게임 입력을 받지 않는다. 켜진 섹션만 4 Hz 로 본문을 부르고 줄을 다시 쓴다.
     *          - 설정 창(`engine/ui/debughud.ui.xml`)은 모달이라 열린 동안만 UI 가 입력을 가진다(`UISystem::isGameInputBlocked`).
     *          - 꺼져 있으면 프레임 비용은 셸 맵 액션 하나를 읽는 것뿐이다(할당 없음).
     *          - `kUsesFrameProfiler` 섹션이 보이는 동안 `FrameProfiler` 를 켜고, HUD 가 켠 것이면 감출 때 끈다.
     *
     *          게임 스레드에서만 쓴다. `Engine` 가 UI 기동 단계에서 만들고 `UISystem::update` 앞에서 `update` 를 부른다.
     */
    class SW_API DebugHUD
    {
    public:
        // 설정 스키마(Dev 기동이 엔진과 게임 스키마 뒤에 덧붙인다)와 섹션 체크 상자 창 문서는 엔진 기본 에셋 표가 기준이다
        // (`EngineDefaultAssets::_debugHUDSettingsSchema` · `_uiDebugHUD`, 창은 `UISystem::getEngineDocuments()._debugHUD`).
        /** @brief 셸 입력 맵(`engine/input/default.input.xml`) Debug 레이어의 마스터 단축키 액션입니다(Ctrl+F3). */
        static constexpr const utf8* kToggleActionName = "DebugHUDToggle";
        /** @brief HUD 패널 위젯 이름입니다(레이아웃 덤프와 감시 값이 찾습니다). */
        static constexpr const utf8* kPanelName = "DebugHUDPanel";
        /** @brief 불투명도 범위입니다(설정 정의와 같으며, 0 이면 HUD 를 켜도 보이지 않습니다). */
        static constexpr float32 kMinOpacity = 0.2f;
        static constexpr float32 kMaxOpacity = 1.0f;

        DebugHUD();
        ~DebugHUD();
        DebugHUD( const DebugHUD& )            = delete;
        DebugHUD& operator=( const DebugHUD& ) = delete;

        /**
         * @brief UI 시스템과 설정 출처를 연결하고, 명령과 감시 값이 찾는 컨트롤러로 등록합니다.
         * @param pSettings 설정 매니저입니다(nullptr 이거나 HUD 스키마를 읽지 않았으면 전역 변수에 바로 씁니다. 테스트용).
         */
        void initialize( UISystem& ui, UserSettingsManager* pSettings );
        /** @brief 화면과 창을 닫고 HUD 가 켠 프로파일러를 끕니다. */
        void shutdown();
        /**
         * @brief 프레임마다(`UISystem::update` 앞) 마스터 단축키, 켜짐 상태, HUD 화면 열기와 닫기, 프로파일러, 닫힌 창의 설정 확정을 순서대로 처리합니다.
         * @param pShellMap 셸 입력 맵입니다(없으면 단축키를 보지 않습니다).
         */
        void update( const InputMap* pShellMap );

        /** @brief HUD 가 켜져 있으면 true 입니다(`gv_debugHUD`). */
        bool isShown() const;
        /** @brief HUD 를 켜거나 끕니다(설정 `debug.hud`, 저장됩니다). 화면은 다음 `update` 가 엽니다. */
        void setShown( bool bShown );
        /** @brief 섹션이 켜져 있으면 true 입니다(설정 텍스트와 등록 기본값 기준). */
        static bool isSectionShown( const DebugHUDSectionRegistration& registration );
        /** @brief 섹션을 켜거나 끕니다(설정 `debug.hudSections`). 등록되지 않은 이름이면 false 입니다. */
        [[nodiscard]] bool setSectionShown( string_view name, bool bShown );
        /** @brief 등록된 섹션 중 켜진 것의 수입니다(HUD 가 꺼져 있어도 셉니다). */
        static uint32         getShownSectionCount();
        static DebugHUDCorner getCorner();
        /** @brief 모서리를 바꿉니다(설정 `debug.hudCorner`). */
        void           setCorner( DebugHUDCorner corner );
        static float32 getOpacity();
        /** @brief 불투명도를 바꿉니다(설정 `debug.hudOpacity`, 0.2..1 로 제한합니다). */
        void setOpacity( float32 opacity );

        /** @brief 설정 창을 엽니다(이미 열려 있으면 그대로 둡니다). 열지 못하면(문서 오류) false 입니다. */
        [[nodiscard]] bool openWindow();
        void               closeWindow();
        bool               isWindowOpen() const;
        /** @brief HUD 화면이 열려 있으면 true 입니다(켜짐과 다음 `update` 사이에는 다를 수 있습니다). */
        bool isScreenOpen() const;
        /** @brief HUD 화면이 지금 그리는 섹션 수입니다(화면이 없으면 0. 감시 값 `DebugHUD.SectionCount`). */
        uint32 getDrawnSectionCount() const;
        /** @brief HUD 화면이 섹션 @p name 을 그리고 있으면 true 입니다(감시 값 `DebugHUD.SectionShown`). */
        bool isSectionDrawn( string_view name ) const;
        /**
         * @brief HUD 패널이 실제로 놓인 모서리를 구합니다(지난 레이아웃 결과이며, 패널 가운데가 뷰포트의 어느 4 분면에 있는지로 판정).
         * @return 화면이 없거나 아직 배치 전이면 false 입니다.
         */
        [[nodiscard]] bool tryGetArrangedCorner( DebugHUDCorner& outCorner ) const;

        /** @brief `topLeft`, `topright`, `0`..`3` 을 모서리로 읽습니다(대소문자 무시). */
        [[nodiscard]] static bool tryParseCorner( string_view text, DebugHUDCorner& outCorner );
        /** @brief 설정 선택지 이름(`topLeft` 등)입니다. */
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
        UIScreenHandle         _screen;                    ///< HUD 화면입니다(오버레이 레이어이며, 없으면 무효).
        UIScreenHandle         _window;                    ///< 설정 창입니다(모달이며, 없으면 무효).
        string                 _profilerSeenState;         ///< `syncFrameProfiler` 가 마지막으로 본 `gv_debugHUDSections` 입니다.
        uint64                 _profilerSeenRevision;      ///< `syncFrameProfiler` 가 마지막으로 본 레지스트리 판 번호입니다(처음은 아무 판과도 다른 값).
        uint8                  _bProfilerEnabledByHUD : 1; ///< HUD 가 프로파일러를 켰는지 나타냅니다(감출 때 끕니다).
        uint8                  _bSectionsNeedProfiler : 1; ///< 켜진 섹션 중 프로파일러를 쓰는 것이 있는지의 캐시입니다(판 번호 · 상태 글이 같으면 다시 세지 않습니다).
        [[maybe_unused]] uint8 _reserved              : 6;
    };
} // namespace sw

#endif
