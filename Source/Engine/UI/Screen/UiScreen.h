/**
 * @file UiScreen.h
 * @brief 화면 하나 — 위젯 트리 하나와 그 성질(층 · 모달 · 포커스 · 커서 · 게임 정지)입니다. `UiSystem` 이 층마다 스택으로 쌓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    class UiSystem;
    class Widget;

    /** @brief 화면 층입니다. 위 층이 아래 층 위에 그려지고 입력을 먼저 받습니다(CommonUI 의 레이어 · Godot CanvasLayer). */
    ENUM()
    enum class UiLayer : uint8
    {
        Hud,      ///< 게임 HUD — 포커스를 받지 않는다(마커 · 체력 · 조준선)
        GameMenu, ///< 게임 안 메뉴(가방 · 지도) — 게임은 멈추지 않는다
        Menu,     ///< 일시정지 · 옵션
        Modal,    ///< 확인 창("유지할까요? N 초") — 아래를 모두 막는다
        Overlay,  ///< 알림 · 토스트 — 입력을 받지 않는다
        Loading   ///< 로딩 화면 — 맨 위, 입력을 막는다
    };

    /** @brief 화면을 가리키는 번호입니다. 다시 쓰지 않습니다(0 = 없음). */
    using UiScreenHandle = uint32;
    /** @brief 화면이 없음을 뜻하는 번호입니다. */
    inline constexpr UiScreenHandle kInvalidUiScreenHandle = 0;
} // namespace sw

namespace sw
{
    /** @struct UiScreenDesc @brief 화면 하나의 성질입니다(문서의 루트 원소 속성으로도 적는다). */
    REFLECT()
    struct SW_API UiScreenDesc
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Layer" )
        UiLayer _layer{ UiLayer::Menu };
        PROPERTY( DisplayName = "Modal", Tooltip = "Blocks input to screens below and to the game" )
        bool _bModal{ false };
        PROPERTY( DisplayName = "Takes Focus", Tooltip = "Receives focus and UI actions (never on the Hud and Overlay layers)" )
        bool _bTakesFocus{ true };
        PROPERTY( DisplayName = "Show Cursor", Tooltip = "Show the OS cursor while this screen is the active screen" )
        bool _bShowCursor{ true };
        PROPERTY( DisplayName = "Pauses Game", Tooltip = "Pause game time while this screen is open" )
        bool _bPausesGame{ false };
        PROPERTY( DisplayName = "Default Focus", Tooltip = "Widget to focus when the screen opens in navigation mode" )
        hashed_string _defaultFocus{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class UiScreen
     * @brief 활성화 가능한 화면 하나입니다(언리얼 CommonUI `UCommonActivatableWidget`). 위젯 트리 하나를 소유합니다.
     * @details 맨 위의 포커스 받는 화면이 입력을 받습니다(`UiSystem` 의 "활성 화면"). 다른 화면이 덮으면 그때의 포커스 위젯을 기억했다가
     *          다시 활성이 되면 돌려줍니다. 닫기(`close`)는 지연입니다 — 사건 처리 중에 닫아도 이번 경로가 끝난 뒤에 지워집니다.
     */
    class SW_API UiScreen
    {
    public:
        UiScreen( const UiScreenDesc& desc, unique_ptr<Widget> root );
        virtual ~UiScreen();
        UiScreen( const UiScreen& )            = delete;
        UiScreen& operator=( const UiScreen& ) = delete;

        WidgetTree&         getTree() { return _tree; }
        const WidgetTree&   getTree() const { return _tree; }
        const UiScreenDesc& getDesc() const { return _desc; }
        UiScreenHandle      getHandle() const { return _handle; }
        UiSystem*           getUiSystem() const { return _pUiSystem; }
        /** @brief 포커스 · UI 행동을 받는 화면인가 — 성질이 그렇고 HUD · 오버레이 층이 아니다. */
        bool takesFocus() const;
        /** @brief 포인터 사건을 받는 화면인가 — 오버레이 층(알림)은 받지 않는다. */
        bool receivesPointer() const { return _desc._layer != UiLayer::Overlay; }
        /** @brief 아래 화면과 게임의 입력을 막는가 — 모달이거나 로딩 층. */
        bool blocksLowerInput() const { return _desc._bModal || _desc._layer == UiLayer::Loading; }
        /** @brief 닫기를 요청했다(이번 입력 처리가 끝나면 지워진다). */
        bool isClosing() const { return _bClosing == SW_TRUE; }
        /** @brief 이 화면을 닫습니다(지연). `UiSystem::closeScreen` 과 같습니다. */
        void close();

        /**
         * @brief `UI.Back` 을 아무 위젯도 처리하지 않았을 때 불립니다. 처리했으면 true 입니다.
         * @details 기본은 스스로 닫기입니다. 확인 창은 "취소" 로, 첫 화면(타이틀)은 아무것도 하지 않도록 덮어씁니다.
         */
        virtual bool onBack();

    private:
        friend class UiSystem;

        WidgetTree     _tree;
        UiScreenDesc   _desc;
        UiSystem*      _pUiSystem; ///< 올린 시스템(올리기 전 nullptr)
        UiScreenHandle _handle;
        WidgetId       _lastFocused; ///< 다른 화면에 덮일 때의 포커스 위젯 — 다시 활성이 되면 돌려준다
        uint32         _pushOrder;   ///< 같은 층 안 쌓인 순서(클수록 위)
        uint8          _bClosing;
    };
} // namespace sw
