/**
 * @file UiScreen.h
 * @brief 화면 하나 — 위젯 트리 하나와 그 성질(층 · 모달 · 포커스 · 커서 · 게임 정지)입니다. `UiSystem` 이 층마다 스택으로 쌓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Core/WidgetTypes.h"
#include "Engine/UI/Document/UiBindingDesc.h"

namespace sw
{
    class UiBindingSet;
    class UiStyleSet;
    class UiSystem;
    class UiViewModel;
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

    /** @brief 위젯이 낸 명령(버튼의 `_command`)을 받는 함수입니다(`UiScreen::registerCommand`). */
    using UiCommandDelegate = Delegate<void( const hashed_string&, Widget& )>;
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
     *
     *          **명령**: 트리의 위젯이 낸 명령(버튼의 `_command` — 클릭 · `UI.Accept`)은 `onCommand` 로 옵니다. 기본은 `registerCommand` 로 건 함수를
     *          부르는 것이고, C++ 화면 클래스는 덮어씁니다(유니티 UI Toolkit 의 컨트롤러가 이름으로 위젯을 찾아 거는 것과 같은 자리).
     *          문서로 연 화면(`UiSystem::openScreen`)은 그 문서 경로와 문서에서 뗀 바인딩 식을 듭니다.
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
        /** @brief 이 화면을 지은 문서 경로입니다(코드로 지은 화면이면 빈 글). */
        const string& getDocumentPath() const { return _documentPath; }
        /** @brief 문서에서 뗀 바인딩 식입니다(위젯 번호가 채워진 것 — 바인딩 단계가 겁니다). */
        const vector<UiBindingDesc>& getBindings() const { return _listBinding; }
        /** @brief 문서(와 그 조각)가 건 스타일 시트 경로입니다(테마 시트 뒤에 붙는다). */
        const vector<string>& getStyleSheets() const { return _listStyleSheet; }
        /** @brief 이 화면의 스타일 묶음(테마 → 문서 시트)입니다. 올리기 전이면 nullptr 입니다. */
        UiStyleSet* getStyleSet() const { return _styleSet.get(); }
        /** @brief 바인딩 식 하나를 더합니다(코드로 지은 화면 — 문서 화면은 `UiSystem::openScreen` 이 넣는다). 다음 바인딩 단계가 다시 겁니다. */
        void addBinding( const UiBindingDesc& binding );
        /**
         * @brief 뷰모델을 겁니다(nullptr 이면 뗀다). 다음 바인딩 단계가 식을 다시 걸고 모든 칸을 씁니다.
         * @details 소유는 게임입니다. 화면보다 먼저 지우면 바인딩이 그 뷰모델을 놓습니다(위젯 값은 마지막 값으로 남는다).
         */
        void                setViewModel( UiViewModel* pViewModel );
        UiViewModel*        getViewModel() const;
        UiBindingSet&       getBindingSet() { return *_bindingSet; }
        const UiBindingSet& getBindingSet() const { return *_bindingSet; }
        /** @brief 트리의 위젯 @p widget 의 칸 @p propertyName 을 사용자 입력이 바꿨다(`Widget::notifyValueEdited`) — 양방향 바인딩이 소스에 되씁니다. */
        void onWidgetValueEdited( Widget& widget, const hashed_string& propertyName );

        /** @brief 명령 @p command 를 받을 함수를 겁니다. 같은 명령에 다시 걸면 바꿉니다. */
        void registerCommand( const hashed_string& command, const UiCommandDelegate& handler );
        /** @brief 명령 @p command 의 함수를 뗍니다. */
        void unregisterCommand( const hashed_string& command );

        /**
         * @brief 트리의 위젯 @p source 가 명령 @p command 를 냈을 때 불립니다. 처리했으면 true 입니다.
         * @details 기본은 `registerCommand` 로 건 함수를 부릅니다. 아무도 처리하지 않으면 경고 한 줄(문서의 명령 이름 오타가 조용히 묻히지 않게).
         */
        virtual bool onCommand( const hashed_string& command, Widget& source );
        /** @brief 트리의 위젯 @p source 가 명령을 냅니다(버튼 클릭) — `onCommand` 로 보내고, 아무도 처리하지 않으면 경고합니다. 빈 명령은 무시합니다. */
        void dispatchCommand( const hashed_string& command, Widget& source );

        /**
         * @brief `UI.Back` 을 아무 위젯도 처리하지 않았을 때 불립니다. 처리했으면 true 입니다.
         * @details 기본은 스스로 닫기입니다. 확인 창은 "취소" 로, 첫 화면(타이틀)은 아무것도 하지 않도록 덮어씁니다.
         */
        virtual bool onBack();

    private:
        friend class UiSystem;

        WidgetTree                                                               _tree;
        UiScreenDesc                                                             _desc;
        string                                                                   _documentPath;   ///< 지은 문서(코드로 지었으면 빈 글)
        vector<UiBindingDesc>                                                    _listBinding;    ///< 문서에서 뗀 바인딩 식
        vector<string>                                                           _listStyleSheet; ///< 문서 · 조각이 건 스타일 시트
        unique_ptr<UiStyleSet>                                                   _styleSet;       ///< 테마 → 문서 시트 묶음(UiSystem 이 건다)
        unique_ptr<UiBindingSet>                                                 _bindingSet;     ///< 바인딩 식을 푼 것(뷰모델 · 설정과 위젯 칸을 잇는다)
        unordered_map<hashed_string, UiCommandDelegate, hashed_string::HashFunc> _mapCommandToHandler;
        UiSystem*                                                                _pUiSystem; ///< 올린 시스템(올리기 전 nullptr)
        UiScreenHandle                                                           _handle;
        WidgetId                                                                 _lastFocused; ///< 다른 화면에 덮일 때의 포커스 위젯 — 다시 활성이 되면 돌려준다
        uint32                                                                   _pushOrder;   ///< 같은 층 안 쌓인 순서(클수록 위)
        uint8                                                                    _bClosing;
    };
} // namespace sw
