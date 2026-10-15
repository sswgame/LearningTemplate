/**
 * @file UIScreen.h
 * @brief 화면 하나를 정의합니다. 위젯 트리 하나와 그 성질(레이어, 모달, 포커스, 커서, 게임 정지)을 가지며, `UISystem` 이 레이어마다 스택으로 쌓습니다.
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
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Document/UIBindingDesc.h"

namespace sw
{
    class UIBindingSet;
    class UIInputRouter;
    class UIStyleSet;
    class UISystem;
    class UIThemeController;
    class UIViewModel;
    class Widget;

    /** @brief 화면 레이어입니다. 위 레이어가 아래 레이어 위에 그려지고 입력을 먼저 받습니다(CommonUI 의 레이어, Godot 의 CanvasLayer). */
    ENUM()
    enum class UILayer : uint8
    {
        HUD,      ///< 게임 HUD 입니다. 포커스를 받지 않습니다(마커, 체력, 조준선).
        GameMenu, ///< 게임 안 메뉴(가방, 지도)입니다. 게임은 멈추지 않습니다.
        Menu,     ///< 일시정지와 옵션 메뉴입니다.
        Modal,    ///< 확인 창("유지할까요? N 초")입니다. 아래를 모두 막습니다.
        Overlay,  ///< 알림과 토스트입니다. 입력을 받지 않습니다.
        Loading   ///< 로딩 화면입니다. 맨 위에서 입력을 막습니다.
    };

    /** @brief 화면을 가리키는 번호입니다. 다시 쓰지 않습니다(0 이면 없음). */
    using UIScreenHandle = uint32;
    /** @brief 화면이 없음을 뜻하는 번호입니다. */
    inline constexpr UIScreenHandle kInvalidUIScreenHandle = 0;

    /** @brief 위젯이 낸 명령(버튼의 `_command`)을 받는 함수입니다(`UIScreen::registerCommand`). */
    using UICommandDelegate = Delegate<void( const hashed_string&, Widget& )>;
} // namespace sw

namespace sw
{
    /** @struct UIScreenDesc @brief 화면 하나의 성질입니다(문서의 루트 요소 속성으로도 적습니다). */
    REFLECT()
    struct SW_API UIScreenDesc
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Layer" )
        UILayer _layer{ UILayer::Menu };
        PROPERTY( DisplayName = "Modal", Tooltip = "Blocks input to screens below and to the game" )
        bool _bModal{ false };
        PROPERTY( DisplayName = "Takes Focus", Tooltip = "Receives focus and UI actions (never on the HUD and Overlay layers)" )
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
     * @class UIScreen
     * @brief 활성화할 수 있는 화면 하나입니다(언리얼 CommonUI 의 `UCommonActivatableWidget`). 위젯 트리 하나를 소유합니다.
     * @details 맨 위에서 포커스를 받는 화면이 입력을 받는다(`UISystem` 의 "활성 화면"). 다른 화면이 덮으면 그때의 포커스 위젯을 기억했다가
     *          다시 활성이 되면 돌려준다. 닫기(`close`)는 지연 처리하므로, 이벤트 처리 중에 닫아도 이번 경로가 끝난 뒤에 지워진다.
     *
     *          **명령**: 트리의 위젯이 낸 명령(버튼의 `_command`, 클릭, `UI.Accept`)은 `onCommand` 로 온다. 기본 구현은 `registerCommand` 로 등록한 함수를
     *          부르고, C++ 화면 클래스는 이를 재정의한다(유니티 UI Toolkit 의 컨트롤러가 이름으로 위젯을 찾아 등록하는 것에 해당한다).
     *          문서로 연 화면(`UISystem::openScreen`)은 그 문서 경로와 문서에서 분리한 바인딩 식을 보관한다.
     *
     *          **애니메이션**: 화면마다 재생기 하나(`getAnimationPlayer`)가 문서의 `<_listAnimation>` 을 가진다. `Open` 은 올릴 때, `Close` 는 닫을 때
     *          재생하고, 닫기는 `Close` 가 끝난 뒤에 실제로 지운다(그동안 화면은 입력과 포커스를 받지 않는다).
     */
    class SW_API UIScreen
    {
    public:
        UIScreen( const UIScreenDesc& desc, unique_ptr<Widget> root );
        virtual ~UIScreen();
        UIScreen( const UIScreen& )            = delete;
        UIScreen& operator=( const UIScreen& ) = delete;

        WidgetTree&         getTree() { return _tree; }
        const WidgetTree&   getTree() const { return _tree; }
        const UIScreenDesc& getDesc() const { return _desc; }
        UIScreenHandle      getHandle() const { return _handle; }
        UISystem*           getUISystem() const { return _pUISystem; }
        /** @brief 포커스와 UI 액션을 받는 화면이면 true 입니다. 성질이 그렇고 HUD 나 오버레이 레이어가 아니어야 합니다. */
        bool takesFocus() const;
        /** @brief 포인터 이벤트를 받는 화면이면 true 입니다. 오버레이 레이어(알림)는 받지 않습니다. */
        bool receivesPointer() const { return _desc._layer != UILayer::Overlay; }
        /** @brief 아래 화면과 게임의 입력을 막는 화면이면 true 입니다. 모달이거나 로딩 레이어입니다. */
        bool blocksLowerInput() const { return _desc._bModal || _desc._layer == UILayer::Loading; }
        /** @brief 닫기를 요청했는지 나타냅니다(이번 입력 처리가 끝나면 지워집니다). */
        bool isClosing() const { return _bClosing == SW_TRUE; }
        /** @brief 이 화면을 닫습니다(지연). `UISystem::closeScreen` 과 같습니다. */
        void close();
        /** @brief 이 화면을 생성한 문서 경로입니다(코드로 만든 화면이면 빈 문자열). */
        const string& getDocumentPath() const { return _documentPath; }
        /** @brief 문서에서 분리한 바인딩 식입니다(위젯 번호가 채워진 것이며, 바인딩 단계가 연결합니다). */
        const vector<UIBindingDesc>& getBindings() const { return _listBinding; }
        /** @brief 문서(와 그 조각)가 지정한 스타일 시트 경로입니다(테마 시트 뒤에 붙습니다). */
        const vector<string>& getStyleSheets() const { return _listStyleSheet; }
        /** @brief 이 화면의 스타일 세트(테마, 문서 시트 순서)입니다. 올리기 전이면 nullptr 입니다. */
        UIStyleSet* getStyleSet() const { return _styleSet.get(); }
        /** @brief 바인딩 식 하나를 추가합니다(코드로 만든 화면용이며, 문서 화면은 `UISystem::openScreen` 이 넣습니다). 다음 바인딩 단계가 다시 연결합니다. */
        void addBinding( const UIBindingDesc& binding );
        /** @brief 트리에 없는 위젯(코드가 지운 행)에 붙은 바인딩 식을 제거합니다. 제거했으면 다음 바인딩 단계가 다시 연결합니다. */
        void removeOrphanBindings();
        /**
         * @brief 뷰모델을 연결합니다(nullptr 이면 해제). 다음 바인딩 단계가 식을 다시 연결하고 모든 필드를 씁니다.
         * @details 소유는 게임이 한다. 화면보다 먼저 지우면 바인딩이 그 뷰모델을 놓는다(위젯 값은 마지막 값으로 남는다).
         */
        void                setViewModel( UIViewModel* pViewModel );
        UIViewModel*        getViewModel() const;
        UIBindingSet&       getBindingSet() { return *_bindingSet; }
        const UIBindingSet& getBindingSet() const { return *_bindingSet; }
        /** @brief 트리의 위젯 @p widget 의 필드 @p propertyName 을 사용자 입력이 바꿨을 때 부릅니다(`Widget::notifyValueEdited`). 양방향 바인딩이 소스에 값을 되씁니다. */
        void onWidgetValueEdited( Widget& widget, const hashed_string& propertyName );
        /** @brief 이 화면의 애니메이션과 트윈 재생기입니다(`UISystem::update` 의 애니메이션 단계가 진행합니다). */
        UIAnimationPlayer&       getAnimationPlayer() { return _animationPlayer; }
        const UIAnimationPlayer& getAnimationPlayer() const { return _animationPlayer; }

        /** @brief 명령 @p command 를 받을 함수를 등록합니다. 같은 명령에 다시 등록하면 교체합니다. */
        void registerCommand( const hashed_string& command, const UICommandDelegate& handler );
        /** @brief 명령 @p command 의 함수를 해제합니다. */
        void unregisterCommand( const hashed_string& command );

        /**
         * @brief 트리의 위젯 @p source 가 명령 @p command 를 냈을 때 불립니다. 처리했으면 true 입니다.
         * @details 기본 구현은 `registerCommand` 로 등록한 함수를 부른다. 아무도 처리하지 않으면 경고를 한 줄 남겨, 문서의 명령 이름 오타가 조용히 묻히지 않게 한다.
         */
        virtual bool onCommand( const hashed_string& command, Widget& source );
        /** @brief 트리의 위젯 @p source 가 명령(버튼 클릭)을 냅니다. `onCommand` 로 보내고, 처리하지 않으면 UI 시스템의 전역 명령 표(`UISystem::getGlobalCommands`)로 보내며, 거기서도 없으면 경고합니다. 빈 명령은 무시합니다. */
        void dispatchCommand( const hashed_string& command, Widget& source );
        /**
         * @brief 처리하지 않은 명령을 경고로 남길지 정합니다(기본 true).
         * @details 게임 코드 없이 문서만 띄우는 화면(에디터 UI 미리보기)은 명령을 받을 쪽이 원래 없으므로 끈다 — 그때는 Info 로 남긴다.
         */
        void setReportsUnhandledCommands( bool bReport ) { _bReportsUnhandledCommands = bReport ? SW_TRUE : SW_FALSE; }

        /**
         * @brief `UI.Back` 을 아무 위젯도 처리하지 않았을 때 불립니다. 처리했으면 true 입니다.
         * @details 기본 구현은 스스로 닫는다. 확인 창은 "취소" 로, 첫 화면(타이틀)은 아무것도 하지 않도록 재정의한다.
         */
        virtual bool onBack();
        /**
         * @brief 화면 단위 액션 @p action(`UI.TabNext`, `UI.TabPrevious`)을 포커스 경로의 위젯이 처리하지 않았을 때 불립니다. 처리했으면 true 입니다.
         * @details 탭 줄 옮기기처럼 포커스와 무관한 화면의 액션이다(CommonUI 화면이 묶는 액션). 기본 구현은 처리하지 않는다.
         */
        virtual bool onUnhandledAction( const hashed_string& action );
        /** @brief `UISystem::update` 마다 바인딩 앞에서 불립니다. 시간에 따라 바꿔야 하는 것(카운트다운, 키 받기)을 처리하는 곳입니다. 기본은 아무것도 하지 않습니다. */
        virtual void onTick( float32 deltaSeconds );
        /** @brief 이 화면이 활성인 동안 UI 액션(탐색, 확인, 뒤로)을 받는지 나타냅니다. 원시 입력을 받는 화면(키 바인딩 창)이 false 로 액션을 끕니다. */
        virtual bool wantsUIActions() const { return true; }
        /** @brief 핫 리로드가 문서로 트리를 새로 만든 뒤 불립니다. 코드로 덧붙인 위젯(옵션 메뉴의 행)을 다시 만드는 곳입니다. 기본은 아무것도 하지 않습니다. */
        virtual void onTreeRebuilt();

    private:
        friend class UISystem;
        friend class UIInputRouter;     // 포커스 복원이 `_lastFocused` 를 읽는다
        friend class UIThemeController; // 스타일 세트를 짓는다

        WidgetTree                                                               _tree;
        UIScreenDesc                                                             _desc;
        string                                                                   _documentPath;    ///< 생성한 문서입니다(코드로 만들었으면 빈 문자열).
        vector<UIBindingDesc>                                                    _listBinding;     ///< 문서에서 분리한 바인딩 식입니다.
        vector<string>                                                           _listStyleSheet;  ///< 문서와 조각이 지정한 스타일 시트입니다.
        unique_ptr<UIStyleSet>                                                   _styleSet;        ///< 테마, 문서 시트 순서의 스타일 세트입니다(UISystem 이 설정합니다).
        unique_ptr<UIBindingSet>                                                 _bindingSet;      ///< 바인딩 식을 해석한 것입니다(뷰모델, 설정과 위젯 필드를 연결합니다).
        UIAnimationPlayer                                                        _animationPlayer; ///< 문서 애니메이션과 코드 트윈 재생기입니다(트리를 가리키므로 `_tree` 뒤에 생성합니다).
        unordered_map<hashed_string, UICommandDelegate, hashed_string::HashFunc> _mapCommandToHandler;
        UISystem*                                                                _pUISystem; ///< 이 화면을 올린 시스템입니다(올리기 전에는 nullptr).
        UIScreenHandle                                                           _handle;
        WidgetID                                                                 _lastFocused; ///< 다른 화면에 덮일 때의 포커스 위젯입니다. 다시 활성이 되면 돌려줍니다.
        uint32                                                                   _pushOrder;   ///< 같은 레이어 안의 쌓인 순서입니다(클수록 위).
        uint8                                                                    _bClosing;
        uint8                                                                    _bReportsUnhandledCommands; ///< 처리하지 않은 명령을 경고로 남긴다(`setReportsUnhandledCommands`)
    };
} // namespace sw
