/**
 * @file EditorCommandRegistry.h
 * @brief 에디터 커맨드의 단일 정의(SSOT)입니다. 메뉴 · 전역 단축키 · 커맨드 팔레트가 같은 정의를 읽습니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
    /**
     * @brief 단축키의 키 코드입니다.
     * @details ImGui 헤더에 의존하지 않도록 자체 열거형을 쓴다. 이 파일이 ImGui 없이 컴파일되어야 단위 테스트를 붙일 수 있다.
     *          ImGuiKey 로 변환하는 곳은 이 값을 쓰는 유일한 위치인 `EditorCommandGUI` 뿐이다.
     */
    enum class EditorCommandKey : uint8
    {
        None = 0,
        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        Space,
        Num0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,
        Delete,
        Insert,
        Home,
        End,
        PageUp,
        PageDown,
        Left,
        Right,
        Up,
        Down,
        Tab,
        Enter,
        Escape,
        Backspace,
        Minus,
        Equal,
        Comma,
        Period,
        Slash,
        Count
    };

    // 이 열거형의 번호는 계약이다. 두 곳이 값의 순서에 기대고 있다.
    //   - `EditorCommandRegistry.cpp` 의 이름 테이블은 열거형 값을 그대로 인덱스로 쓴다.
    //   - `EditorCommandGUI.cpp` 의 `toImGuiKey` 는 A..Z 와 F1..F12 를 뺄셈으로 변환한다.
    // 가운데에 값을 하나 끼우면 조용히 엉뚱한 키가 되어 단축키가 다른 명령을 실행한다. 개수만 보는 static_assert 로는
    // 이를 잡지 못하므로(이름을 하나 더하면 개수는 다시 맞는다) 위치를 직접 고정한다. 키를 추가하려면 Space 앞이 아니라
    // Space 뒤에 붙이고 아래 static_assert 를 고친다. Space 뒤의 키(Num0 등)는 `EditorCommandGUI.cpp` 의 테이블(`kArrExtraKey`)이 ImGuiKey 로 변환한다.
    static_assert( static_cast<uint8>( EditorCommandKey::A ) == 1, "EditorCommandKey::A 의 자리가 바뀌었습니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::Z ) == 26, "EditorCommandKey 의 A..Z 가 연속이 아닙니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::F1 ) == 27, "EditorCommandKey::F1 의 자리가 바뀌었습니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::F12 ) == 38, "EditorCommandKey 의 F1..F12 가 연속이 아닙니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::Space ) == 39, "EditorCommandKey::Space 의 자리가 바뀌었습니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::Num0 ) == 40, "EditorCommandKey::Num0 의 자리가 바뀌었습니다" );

    /** @brief 단축키 수정자 비트입니다. */
    namespace commandmodifier
    {
        inline constexpr uint8 kNone  = 0;
        inline constexpr uint8 kCtrl  = static_cast<uint8>( SW_BIT( 0 ) );
        inline constexpr uint8 kShift = static_cast<uint8>( SW_BIT( 1 ) );
        inline constexpr uint8 kAlt   = static_cast<uint8>( SW_BIT( 2 ) );
        /**
         * @brief 라벨에만 표시하고 엔진이 처리하지 않는 조합입니다.
         * @details Alt+F4 처럼 OS 가 먼저 가로채는 조합을 메뉴에 표시하기 위한 비트다. 이 비트가 있으면 단축키 처리기가 건너뛴다.
         *          처리하지 않는 조합이 처리하는 것처럼 보이는 라벨을 막는다.
         */
        inline constexpr uint8 kDisplayOnly = static_cast<uint8>( SW_BIT( 3 ) );
    } // namespace commandmodifier

    /** @brief 메뉴 경로 규칙입니다. 경로는 `"<부모>/<메뉴 이름>"` 이고, 메인 메뉴바의 메뉴는 부모가 `kMainMenuBar` 입니다. */
    namespace commandmenu
    {
        inline constexpr const utf8* kMainMenuBar = "MainMenu";
        /** @brief 뷰포트 툴바의 "Align..." 팝업이 그리는 메뉴입니다. */
        inline constexpr const utf8* kViewportAlign = "Viewport/Align";
        /**
         * @brief 메인 메뉴바 밖에서 코드가 직접 그리는 메뉴 경로(`EditorCommandGUI::drawMenuItems`) 목록입니다. 부모가 `kMainMenuBar` 가 아닌 메뉴는 여기에 있어야 그려집니다.
         * @details 메인 메뉴바는 한 단계 메뉴만 그린다(하위 메뉴 없음). 커맨드 테이블의 경로가 이 목록에도 메인 메뉴바에도 없으면 그 항목은 화면 어디에도
         *          나오지 않으므로 `EditorCommandRegistry::validate` 가 잡는다. 코드가 그리는 경로는 문자열을 두 곳에 적지 않도록 이 상수로 적는다.
         */
        inline constexpr const utf8* kArrHostedMenuPath[] = { kViewportAlign };
        /** @brief 메뉴 순서를 묶는 폭입니다. 같은 메뉴에서 이웃한 두 항목의 `_menuOrder / kGroupSpan` 이 다르면 사이에 구분선이 들어갑니다. */
        inline constexpr int32 kGroupSpan = 100;
    } // namespace commandmenu

    /** @brief 키 조합 하나를 나타냅니다. */
    struct EditorCommandShortcut
    {
        EditorCommandKey _key{ EditorCommandKey::None };
        uint8            _modifier{ commandmodifier::kNone };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 대상을 받는 명령 동작입니다. 전역 명령은 대상을 무시한다(`EditorCommandTarget::none()` 을 받는다). */
    using EditorCommandActionFn = void ( * )( const EditorCommandTarget& target );
    /** @brief 대상을 받는 명령 켜짐 조건입니다. */
    using EditorCommandEnabledFn = bool ( * )( const EditorCommandTarget& target );

    /**
     * @struct EditorCommandTargetUtil
     * @brief 인자 없는 전역 동작 · 켜짐 함수를 대상을 받는 형식으로 맞춥니다(등록 줄 `SW_EDITOR_COMMAND` 가 부른다).
     * @details 전역 명령마다 대상을 버리는 감싸개를 손으로 쓰면 이름만 늘어난다. 등록 줄이 함수의 형식을 보고 고른다 —
     *          대상을 받는 함수는 그대로, 인자 없는 함수는 대상을 버리는 감싸개로, nullptr 은 nullptr 로.
     */
    struct EditorCommandTargetUtil
    {
        template <auto TFunction>
        static void ignoreTargetAction( const EditorCommandTarget& /*target*/ )
        {
            TFunction();
        }
        template <auto TFunction>
        static bool ignoreTargetEnabled( const EditorCommandTarget& /*target*/ )
        {
            return TFunction();
        }
        template <auto TFunction>
        static constexpr EditorCommandActionFn toAction()
        {
            if constexpr ( std::is_null_pointer_v<decltype( TFunction )> )
                return nullptr;
            else if constexpr ( std::is_convertible_v<decltype( TFunction ), EditorCommandActionFn> )
                return TFunction;
            else
                return &ignoreTargetAction<TFunction>;
        }
        template <auto TFunction>
        static constexpr EditorCommandEnabledFn toEnabled()
        {
            if constexpr ( std::is_null_pointer_v<decltype( TFunction )> )
                return nullptr;
            else if constexpr ( std::is_convertible_v<decltype( TFunction ), EditorCommandEnabledFn> )
                return TFunction;
            else
                return &ignoreTargetEnabled<TFunction>;
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 커맨드가 씬을 바꾸는지입니다. 모든 커맨드가 정한다(테이블 줄 · `SW_EDITOR_COMMAND` 의 필수 칸 — 빠뜨리면 컴파일되지 않는다).
     * @details `Scene` 이면 레지스트리가 커맨드 자신의 켜짐 조건과 함께 편집 허용 답(`EditorPlaySession::areSceneEditsAllowed` — 플레이 중 · 베이크처럼 편집을
     *          막는 작업 중이면 false)을 묻는다(`EditorCommandRegistry::isEnabled`). 커맨드마다 그 답을 기억해 묻지 않게 하는 한 창구다.
     */
    enum class EditorCommandEdit : uint8
    {
        NoScene, ///< 씬을 바꾸지 않는다(창 열기 · 저장 · 빌드 · 플레이 조작)
        Scene,   ///< 활성 씬의 오브젝트를 바꾼다(붙여넣기 · 정렬 · 베이크 · Undo)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 에디터 커맨드 하나의 정의입니다.
     * @details 한 커맨드는 메뉴바, 전역 단축키, 커맨드 팔레트의 최대 세 곳에 나타난다. 곳마다 따로 적으면 서로 어긋나므로
     *          정의는 여기에 한 번만 둔다.
     */
    struct EditorCommandDesc
    {
        string                _id;       ///< "scene.save" 처럼 점으로 구분한 고유 id
        string                _label;    ///< 메뉴와 팔레트에 보이는 이름
        string                _icon;     ///< 메뉴 라벨 앞에 붙는 아이콘 글리프. 비어도 된다
        string                _category; ///< 팔레트 그룹 이름
        string                _tooltip;  ///< 메뉴 항목 툴팁(한국어)
        string                _detail;   ///< 팔레트 오른쪽 설명(영어)
        EditorCommandShortcut _shortcut;
        EditorCommandShortcut _altShortcut;        ///< 같은 커맨드의 두 번째 조합. 없으면 None
        EditorCommandShortcut _defaultShortcut;    ///< 사용자가 덮어쓰기 전의 조합(커맨드 테이블, 등록 항목). Reset 이 쓰며 등록할 때 채운다
        EditorCommandShortcut _defaultAltShortcut; ///< 사용자가 덮어쓰기 전의 보조 조합
        /**
         * @brief 메뉴 안의 순서입니다. 작을수록 위이고, 백의 자리(`commandmenu::kGroupSpan`)가 바뀌는 위치에 구분선이 들어갑니다.
         * @details 메뉴바에서 메뉴끼리의 순서(왼쪽에서 오른쪽)도 이 값으로 정한다. 가장 작은 값이 더 작은 메뉴가 앞에 온다.
         */
        int32                                        _menuOrder{ 0 };
        Delegate<void( const EditorCommandTarget& )> _action;                             ///< 대상을 받는 동작. 전역 명령은 대상을 무시한다
        Delegate<bool( const EditorCommandTarget& )> _enabledPredicate;                   ///< 바인딩하지 않으면 항상 활성이다
        EditorCommandEdit                            _edit{ EditorCommandEdit::NoScene }; ///< `Scene` 이면 편집 허용 답도 묻는다(`EditorCommandEdit`)
        /** @brief 이 커맨드가 놓일 메뉴 경로(`"MainMenu/File"`)입니다. 비어 있으면 어느 메뉴에도 나오지 않습니다. */
        string _menuPath;
        bool   _bPaletteVisible{ true };
        /**
         * @brief 이 커맨드가 놓일 맥락(우클릭) 메뉴의 종류입니다. `None` 이면 어느 맥락 메뉴에도 나오지 않습니다.
         * @details 메뉴 종류는 대상 종류다 — 같은 종류는 어느 패널에서 열어도 같은 메뉴다(EditorUX 10-3 R5). 순서 · 구분선 규칙은 메뉴바와 같다(`_contextMenuOrder`).
         */
        EditorCommandTargetKind _contextMenuKind{ EditorCommandTargetKind::None };
        int32                   _contextMenuOrder{ 0 }; ///< 맥락 메뉴 안 순서. 백의 자리가 바뀌면 구분선이 들어간다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 메뉴 안의 항목 하나입니다. */
    struct EditorMenuItem
    {
        uint32 _commandIndex;     ///< `EditorCommandRegistry::getCommands()` 의 인덱스
        bool   _bSeparatorBefore; ///< 앞 항목과 그룹이 달라 사이에 구분선을 그린다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 커맨드의 메뉴 경로 필드에서 만든 메뉴 하나입니다. */
    struct EditorMenu
    {
        string                 _path;       ///< "MainMenu/File"
        string                 _parentPath; ///< "MainMenu"
        string                 _name;       ///< "File" (메뉴에 보이는 이름)
        vector<EditorMenuItem> _listItem;   ///< `_menuOrder` 순서
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorCommandRegistry
     * @brief 에디터 커맨드 정의를 모아 두고 id 로 찾아 실행합니다. `EditorContext` 가 소유합니다.
     */
    class SW_EDITOR_API EditorCommandRegistry : public EditorSubsystem
    {
    public:
        EditorCommandRegistry()                                          = default;
        EditorCommandRegistry( const EditorCommandRegistry& )            = delete;
        EditorCommandRegistry& operator=( const EditorCommandRegistry& ) = delete;
        ~EditorCommandRegistry() override                                = default;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorCommandRegistry";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief 등록된 커맨드를 모두 지웁니다. */
        void clear();
        /** @brief 커맨드를 등록하고 메뉴를 다시 만듭니다. id 가 비어 있으면 무시합니다. */
        void registerCommand( EditorCommandDesc desc );

        /** @brief id 로 커맨드를 찾습니다. 없으면 nullptr 입니다. */
        const EditorCommandDesc* find( string_view commandID ) const;
        /** @brief @p target 에 대한 활성 조건을 평가합니다. 조건이 없으면 true 입니다. */
        static bool isEnabled( const EditorCommandDesc& desc, const EditorCommandTarget& target );
        /** @brief 활성 상태이면 @p target 에 커맨드를 실행하고 true 를 반환합니다. 대상이 없는 실행은 `EditorCommandTarget::none()` 을 넘깁니다. */
        bool execute( string_view commandID, const EditorCommandTarget& target );
        /** @brief 활성 상태이면 @p target 에 커맨드를 실행하고 true 를 반환합니다. */
        static bool executeDesc( const EditorCommandDesc& desc, const EditorCommandTarget& target );
        /**
         * @brief @p kind 맥락 메뉴에 놓인 커맨드를 `_contextMenuOrder` 순서로 @p outListItem 에 담습니다. 구분선 규칙은 메뉴바와 같습니다.
         * @details 항목은 커맨드 인덱스만 가리킨다 — 라벨 · 켜짐 · 단축키 글자는 커맨드가 정한다(R4). 패널이 메뉴 글자를 적지 않는다.
         */
        void collectContextMenuItems( EditorCommandTargetKind kind, vector<EditorMenuItem>& outListItem ) const;

        const vector<EditorCommandDesc>& getCommands() const { return _listCommand; }
        /** @brief 커맨드의 메뉴 경로와 순서 필드에서 만든 메뉴 목록입니다. 메뉴 안에서 가장 작은 `_menuOrder` 가 작은 메뉴가 앞에 옵니다. */
        const vector<EditorMenu>& getMenus() const { return _listMenu; }
        /** @brief 경로로 메뉴를 찾습니다. 그 경로에 커맨드가 하나도 없으면 nullptr 입니다. */
        const EditorMenu* findMenu( string_view menuPath ) const;

        /**
         * @brief 중복 id, 중복 키 조합, 같은 메뉴의 같은 순서, 어디서도 그리지 않는 메뉴 경로를 검사해 outReport 에 적습니다. 문제가 없으면 true 입니다.
         * @details 같은 조합을 두 곳이 처리하는 충돌을 기계적으로 잡는다. 예를 들어 Ctrl+Z 를 전역과 Inspector 가 각각 undo 로 처리하면 두 번 되돌려진다.
         */
        bool validate( string& outReport ) const;

        /** @brief "Ctrl+Shift+B" 또는 "Ctrl+Alt+F11 / F7" 형태의 단축키 라벨을 만듭니다. */
        static void formatShortcutLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer64>& outLabel );
        /** @brief 아이콘과 라벨을 합친 메뉴 표시 문자열을 만듭니다. */
        static void formatMenuLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer128>& outLabel );
        /** @brief 키 하나의 표시 이름을 반환합니다. None 이면 빈 문자열입니다. */
        static const utf8* getKeyName( EditorCommandKey key );
        /** @brief 표시 이름으로 키를 찾습니다. `getKeyName` 의 반대 방향이며 대소문자를 무시합니다. 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool findKeyByName( string_view name, EditorCommandKey& outKey );
        /** @brief 사용자 덮어쓰기로 단축키를 바꿉니다. 그 id 가 없으면 false 입니다. 메뉴는 그대로입니다. */
        [[nodiscard]] bool setShortcut( string_view commandID, const EditorCommandShortcut& shortcut, const EditorCommandShortcut& altShortcut );
        /** @brief 두 조합이 같은 키와 같은 수정자이면 true 입니다. */
        static bool isSameShortcut( const EditorCommandShortcut& lhs, const EditorCommandShortcut& rhs );
        /** @brief 엔진이 실제로 처리하는 조합이면 true 입니다. 키가 있고 DisplayOnly 가 아니어야 합니다. */
        static bool isHandledShortcut( const EditorCommandShortcut& shortcut );
        /**
         * @brief 지금 눌린 수정자 @p pressedModifier(`commandmodifier` 비트)가 @p shortcut 이 요구하는 수정자와 정확히 같으면 true 입니다.
         * @details 요구하지 않은 수정자가 눌려 있으면 false 이다. 필요한 수정자만 확인하면 Ctrl+Shift+Z 가 Ctrl+Z(undo)까지 함께 발동한다.
         *          Super(Win 키)는 대응하는 수정자가 없으므로 눌려 있으면 항상 false 이다. Win+Z 가 Ctrl+Z 로 발동하지 않게 하기 위해서다.
         */
        static bool matchesPressedModifiers( const EditorCommandShortcut& shortcut, uint8 pressedModifier, bool bSuperDown );

    private:
        /** @brief `_listCommand` 의 메뉴 경로와 순서 필드에서 `_listMenu` 를 다시 만듭니다. */
        void rebuildMenus();

    private:
        vector<EditorCommandDesc> _listCommand;
        vector<EditorMenu>        _listMenu;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorCommandRegistration
     * @brief 확장 모듈이나 EditorModule 의 임의의 파일이 커맨드 하나를 추가하는 등록 항목입니다.
     * @details 커맨드 테이블(`EditorCommandGUI.cpp` 의 `_s_arrCommandRow`)과 같은 값을 가지며, 커맨드 레지스트리는 테이블과 등록 항목을 합쳐 메뉴, 단축키, 팔레트를 만든다.
     *          id 는 테이블의 id 와도 겹치면 안 된다(`validate`). 문자열은 리터럴이어야 한다. 메뉴 경로는 이미 있는 메뉴(`"MainMenu/File"` 등)이거나
     *          새 한 단계 메뉴(`"MainMenu/ThemePark"`)이다. 메뉴끼리는 가장 작은 순서값으로 정렬되므로 확장은 9000번대를 쓴다.
     */
    struct EditorCommandRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "command";

        const utf8*            _pLabel;
        const utf8*            _pIcon;
        const utf8*            _pCategory;
        const utf8*            _pTooltip;
        const utf8*            _pDetail;
        EditorCommandShortcut  _shortcut;
        EditorCommandActionFn  _pfnAction;  ///< 대상을 받는 동작(등록 줄이 인자 없는 함수를 `EditorCommandTargetUtil::toAction` 으로 맞춘다)
        EditorCommandEnabledFn _pfnEnabled; ///< nullptr 이면 늘 켜져 있다
        EditorCommandEdit      _edit;       ///< 씬을 바꾸는지(필수 — `EditorCommandEdit`)
        const utf8*            _pMenuPath;  ///< nullptr 이면 메뉴에 나오지 않는다(팔레트와 단축키만 쓴다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorCommandTableUtil
     * @brief 커맨드 등록 항목(`SW_EDITOR_COMMAND`)을 레지스트리에 옮깁니다. ImGui 를 쓰지 않으므로 EditorTest 가 테스트합니다.
     */
    struct SW_EDITOR_API EditorCommandTableUtil
    {
        /**
         * @brief 등록 항목을 모두 @p registry 에 추가합니다. 항목이 [@p pExcludeBegin, @p pExcludeEnd)(언로드되는 모듈 이미지) 안에 있으면 건너뜁니다.
         * @return 건너뛴 항목 수입니다.
         */
        static uint32 appendRegistrations( EditorCommandRegistry& registry, const void* pExcludeBegin, const void* pExcludeEnd );
    };
} // namespace sw::editor

/**
 * @brief 커맨드 하나를 그 커맨드의 .cpp 에서 등록합니다. @p menuOrder 는 메뉴 안 순서이며 테이블의 `_menuOrder` 와 같은 뜻입니다. 백의 자리가 바뀌면 구분선이 들어갑니다.
 * @code SW_EDITOR_COMMAND( ParkReload, "themepark.reloadLayout", 9100, "Reload Park Layout", editoricon::kRefresh, "ThemePark", "배치 파일을 다시 읽습니다",
 *                          "Reload rides.xml", {}, &reloadLayout, nullptr, "MainMenu/ThemePark" ); @endcode
 */
#define SW_EDITOR_COMMAND( name, pID, menuOrder, pLabel, pIcon, pCategory, pTooltip, pDetail, shortcut, pfnAction, pfnEnabled, edit, pMenuPath )  \
    SW_EDITOR_REGISTER( ::sw::editor::EditorCommandRegistration, Command_##name, { pID, menuOrder }, pLabel, pIcon, pCategory, pTooltip, pDetail, \
                        shortcut, ::sw::editor::EditorCommandTargetUtil::toAction<pfnAction>(),                                                   \
                        ::sw::editor::EditorCommandTargetUtil::toEnabled<pfnEnabled>(), ::sw::editor::EditorCommandEdit::edit, pMenuPath )
