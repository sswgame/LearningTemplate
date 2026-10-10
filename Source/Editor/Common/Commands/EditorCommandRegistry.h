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

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorRegistry.h"

namespace sw::editor
{
    /**
     * @brief 단축키의 키 코드입니다.
     * @details ImGui 헤더에 묶이지 않도록 자체 열거형을 씁니다. 이 파일이 ImGui 없이 컴파일되어야 단위 테스트를 붙일 수
     *          있습니다. ImGuiKey 로의 변환은 이 값을 쓰는 유일한 곳(`EditorCommandGUI`)이 합니다.
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

    // **이 열거형의 번호는 계약이다.** 두 곳이 값의 순서에 기대고 있다.
    //   · `EditorCommandRegistry.cpp` 의 이름 표: 열거형 값을 그대로 첨자로 쓴다.
    //   · `EditorCommandGUI.cpp` 의 `toImGuiKey`: A..Z 와 F1..F12 를 **뺄셈**으로 옮긴다.
    // 둘 다 가운데에 값을 하나 끼우면 **조용히 엉뚱한 키**가 된다(단축키가 다른 명령을 실행한다). 개수만 보는 static_assert
    // 로는 그 경우를 잡지 못하므로(이름을 하나 더하면 개수는 다시 맞는다) 자리를 직접 고정한다. 키를 더하려면 **Space 앞이
    // 아니라 Space 뒤에** 붙이고 여기를 고칠 것. Space 뒤의 키(Num0 …)는 `EditorCommandGUI.cpp` 의 표(`kArrExtraKey`)가 ImGuiKey 로 옮긴다.
    static_assert( static_cast<uint8>( EditorCommandKey::A ) == 1, "EditorCommandKey::A 의 자리가 바뀌었습니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::Z ) == 26, "EditorCommandKey 의 A..Z 가 연속이 아닙니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::F1 ) == 27, "EditorCommandKey::F1 의 자리가 바뀌었습니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::F12 ) == 38, "EditorCommandKey 의 F1..F12 가 연속이 아닙니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::Space ) == 39, "EditorCommandKey::Space 의 자리가 바뀌었습니다" );
    static_assert( static_cast<uint8>( EditorCommandKey::Num0 ) == 40, "EditorCommandKey::Num0 의 자리가 바뀌었습니다" );

    /** @brief 단축키 수정자 비트 */
    namespace commandmodifier
    {
        inline constexpr uint8 kNone  = 0;
        inline constexpr uint8 kCtrl  = static_cast<uint8>( SW_BIT( 0 ) );
        inline constexpr uint8 kShift = static_cast<uint8>( SW_BIT( 1 ) );
        inline constexpr uint8 kAlt   = static_cast<uint8>( SW_BIT( 2 ) );
        /**
         * @brief 라벨에만 보이고 우리가 처리하지 않는 조합입니다.
         * @details Alt+F4 처럼 OS 가 먼저 가로채는 것을 메뉴에 적어 두기 위한 표시입니다. 이 비트가 있으면 단축키 처리기가
         *          건너뜁니다. 처리하는 척하는 라벨이 남지 않게 합니다.
         */
        inline constexpr uint8 kDisplayOnly = static_cast<uint8>( SW_BIT( 3 ) );
    } // namespace commandmodifier

    /** @brief 메뉴 경로의 약속입니다. 경로는 `"<부모>/<메뉴 이름>"` 이고, 메인 메뉴바의 메뉴는 부모가 `kMainMenuBar` 입니다. */
    namespace commandmenu
    {
        inline constexpr const utf8* kMainMenuBar = "MainMenu";
        /** @brief 뷰포트 툴바의 "Align..." 팝업이 그리는 메뉴입니다. */
        inline constexpr const utf8* kViewportAlign = "Viewport/Align";
        /**
         * @brief 메인 메뉴바 밖에서 코드가 그리는 메뉴 경로(`EditorCommandGUI::drawMenuItems`)입니다. 부모가 `kMainMenuBar` 가 아닌 메뉴는 여기 있어야 그려집니다.
         * @details 메인 메뉴바는 한 단계 메뉴만 그립니다(하위 메뉴 없음). 표의 경로가 이 목록에도 메인 메뉴바에도 없으면 그 항목은 화면 어디에도 나오지
         *          않으므로 `EditorCommandRegistry::validate` 가 잡습니다. 코드가 그리는 경로는 이 상수로 적습니다(글자를 두 곳에 적지 않는다).
         */
        inline constexpr const utf8* kArrHostedMenuPath[] = { kViewportAlign };
        /** @brief 메뉴 순서의 묶음 폭입니다. 같은 메뉴에서 이웃한 두 항목의 `_menuOrder / kGroupSpan` 이 다르면 사이에 구분선이 들어갑니다. */
        inline constexpr int32 kGroupSpan = 100;
    } // namespace commandmenu

    /** @brief 키 조합 하나 */
    struct EditorCommandShortcut
    {
        EditorCommandKey _key{ EditorCommandKey::None };
        uint8            _modifier{ commandmodifier::kNone };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 에디터 커맨드 하나의 정의입니다.
     * @details 한 커맨드는 최대 세 곳(메뉴바 · 전역 단축키 · 커맨드 팔레트)에 나타납니다. 곳마다 따로 적으면 서로 어긋나므로
     *          정의는 여기에 한 번만 둡니다.
     */
    struct EditorCommandDesc
    {
        string                _id;       ///< "scene.save" 처럼 점으로 구분한 고유 id
        string                _label;    ///< 메뉴·팔레트에 보이는 이름
        string                _icon;     ///< 메뉴 라벨 앞에 붙는 아이콘 글리프. 비어도 됩니다
        string                _category; ///< 팔레트 묶음 이름
        string                _tooltip;  ///< 메뉴 항목 툴팁 (한국어)
        string                _detail;   ///< 팔레트 오른쪽 설명 (영어)
        EditorCommandShortcut _shortcut;
        EditorCommandShortcut _altShortcut;        ///< 같은 커맨드의 두 번째 조합. 없으면 None
        EditorCommandShortcut _defaultShortcut;    ///< 사용자 덮어쓰기 전의 조합(표 · 등록 줄) — Reset 이 쓴다. 등록할 때 채운다
        EditorCommandShortcut _defaultAltShortcut; ///< 사용자 덮어쓰기 전의 보조 조합
        /**
         * @brief 메뉴 안의 순서입니다. 작을수록 위이고, 백의 자리(`commandmenu::kGroupSpan`)가 바뀌는 자리에 구분선이 들어갑니다.
         * @details 메뉴끼리의 순서(메뉴바의 왼쪽→오른쪽)도 이 값으로 정합니다 — 가장 작은 값이 더 작은 메뉴가 앞입니다.
         */
        int32            _menuOrder{ 0 };
        Delegate<void()> _action;
        Delegate<bool()> _enabledPredicate; ///< 바인딩되지 않으면 항상 활성입니다
        /** @brief 이 커맨드가 놓일 메뉴 경로(`"MainMenu/File"`)입니다. 비면 어느 메뉴에도 나오지 않습니다. */
        string _menuPath;
        bool   _bPaletteVisible{ true };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 메뉴 안의 항목 하나입니다. */
    struct EditorMenuItem
    {
        uint32 _commandIndex;     ///< `EditorCommandRegistry::getCommands()` 의 칸
        bool   _bSeparatorBefore; ///< 앞 항목과 묶음이 달라 사이에 구분선을 그립니다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 커맨드 표의 메뉴 경로 칸에서 만든 메뉴 하나입니다. */
    struct EditorMenu
    {
        string                 _path;       ///< "MainMenu/File"
        string                 _parentPath; ///< "MainMenu"
        string                 _name;       ///< "File" (메뉴에 보이는 이름)
        vector<EditorMenuItem> _listItem;   ///< `_menuOrder` 순
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorCommandRegistry
     * @brief 에디터 커맨드 정의를 모아 두고 id 로 찾아 실행합니다 (EditorContext 소유).
     */
    class SW_EDITOR_API EditorCommandRegistry
    {
    public:
        EditorCommandRegistry()                                          = default;
        EditorCommandRegistry( const EditorCommandRegistry& )            = delete;
        EditorCommandRegistry& operator=( const EditorCommandRegistry& ) = delete;
        ~EditorCommandRegistry()                                         = default;

        /** @brief 등록된 커맨드를 모두 버립니다. */
        void clear();
        /** @brief 커맨드를 등록하고 메뉴를 다시 만듭니다. id 가 비어 있으면 무시합니다. */
        void registerCommand( EditorCommandDesc desc );

        /** @brief id 로 커맨드를 찾습니다. 없으면 nullptr입니다. */
        const EditorCommandDesc* find( string_view commandID ) const;
        /** @brief 활성 조건을 평가합니다. 조건이 없으면 true입니다. */
        static bool isEnabled( const EditorCommandDesc& desc );
        /** @brief 활성 상태이면 실행하고 true입니다. */
        bool execute( string_view commandID );
        /** @brief 활성 상태이면 실행하고 true입니다. */
        static bool executeDesc( const EditorCommandDesc& desc );

        const vector<EditorCommandDesc>& getCommands() const { return _listCommand; }
        /** @brief 커맨드의 메뉴 경로 · 순서 칸에서 만든 메뉴들입니다. 메뉴 안 가장 작은 `_menuOrder` 가 작은 메뉴가 앞입니다. */
        const vector<EditorMenu>& getMenus() const { return _listMenu; }
        /** @brief 경로로 메뉴를 찾습니다. 그 경로에 커맨드가 하나도 없으면 nullptr 입니다. */
        const EditorMenu* findMenu( string_view menuPath ) const;

        /**
         * @brief 중복 id · 중복 키 조합 · 같은 메뉴의 같은 순서 · 어디서도 그리지 않는 메뉴 경로를 outReport 에 적습니다. 문제가 없으면 true입니다.
         * @details 같은 조합을 두 곳이 처리하는 충돌(예: Ctrl+Z 가 전역과 Inspector 에서 각각 undo 를 불러 두 번 되돌림)을
         *          기계적으로 잡습니다.
         */
        bool validate( string& outReport ) const;

        /** @brief "Ctrl+Shift+B" 또는 "Ctrl+Alt+F11 / F7" 형태의 단축키 라벨을 씁니다. */
        static void formatShortcutLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer64>& outLabel );
        /** @brief 아이콘과 라벨을 합친 메뉴 표시 문자열을 씁니다. */
        static void formatMenuLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer128>& outLabel );
        /** @brief 키 하나의 표시 이름입니다. None 이면 빈 문자열입니다. */
        static const utf8* getKeyName( EditorCommandKey key );
        /** @brief 표시 이름으로 키를 찾습니다(`getKeyName` 의 역, 대소문자 무시). 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool findKeyByName( string_view name, EditorCommandKey& outKey );
        /** @brief 단축키를 고칩니다(사용자 덮어쓰기). 그 id 가 없으면 false 입니다. 메뉴는 그대로입니다. */
        [[nodiscard]] bool setShortcut( string_view commandID, const EditorCommandShortcut& shortcut, const EditorCommandShortcut& altShortcut );
        /** @brief 두 조합이 같은 키·같은 수정자이면 true입니다. */
        static bool isSameShortcut( const EditorCommandShortcut& lhs, const EditorCommandShortcut& rhs );
        /** @brief 우리가 실제로 처리하는 조합이면 true입니다 (키가 있고 DisplayOnly 가 아닙니다). */
        static bool isHandledShortcut( const EditorCommandShortcut& shortcut );
        /**
         * @brief 지금 눌린 수정자 @p pressedModifier(`commandmodifier` 비트)가 @p shortcut 이 요구하는 것과 **정확히** 같으면 true입니다.
         * @details 요구하지 않은 수정자가 눌려 있어도 false 입니다 — 필요한 것만 보면 Ctrl+Shift+Z 가 Ctrl+Z(undo)까지 함께 발동합니다.
         *          Super(Win 키)는 대응하는 수정자가 없어 눌려 있으면 늘 false 입니다(Win+Z 가 Ctrl+Z 로 발동하지 않게).
         */
        static bool matchesPressedModifiers( const EditorCommandShortcut& shortcut, uint8 pressedModifier, bool bSuperDown );

    private:
        /** @brief `_listCommand` 의 메뉴 경로 · 순서 칸에서 `_listMenu` 를 다시 만듭니다. */
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
     * @brief 확장 모듈(또는 EditorModule 의 아무 파일)이 커맨드 하나를 더하는 등록 줄입니다.
     * @details 표(`EditorCommandGUI.cpp` 의 `_s_arrCommandRow`)와 같은 값을 가지고, 커맨드 레지스트리는 표와 등록 줄을 합쳐 메뉴, 단축키, 팔레트를 만듭니다.
     *          id 는 표와도 겹치면 안 됩니다(`validate`). 문자열은 리터럴이어야 합니다. 메뉴 경로는 이미 있는 메뉴(`"MainMenu/File"` …)이거나
     *          새 한 단계 메뉴(`"MainMenu/ThemePark"`)입니다. 메뉴끼리는 가장 작은 순서로 줄 서므로 확장은 9000 대를 씁니다.
     */
    struct EditorCommandRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "command";

        const utf8*           _pLabel;
        const utf8*           _pIcon;
        const utf8*           _pCategory;
        const utf8*           _pTooltip;
        const utf8*           _pDetail;
        EditorCommandShortcut _shortcut;
        void ( *_pfnAction )();
        bool ( *_pfnEnabled )();
        const utf8* _pMenuPath; ///< nullptr 이면 메뉴에 없습니다(팔레트와 단축키만)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorCommandTableUtil
     * @brief 커맨드 등록 줄(`SW_EDITOR_COMMAND`)을 레지스트리에 옮깁니다. ImGui 를 쓰지 않으므로 EditorTest 가 시험합니다.
     */
    struct SW_EDITOR_API EditorCommandTableUtil
    {
        /**
         * @brief 등록 줄을 모두 @p registry 에 더합니다. 등록 줄이 [@p pExcludeBegin, @p pExcludeEnd)(언로드되는 모듈 이미지) 안이면 건너뜁니다.
         * @return 건너뛴 줄 수입니다.
         */
        static uint32 appendRegistrations( EditorCommandRegistry& registry, const void* pExcludeBegin, const void* pExcludeEnd );
    };
} // namespace sw::editor

/**
 * @brief 커맨드 하나를 그 커맨드의 .cpp 에서 등록합니다. @p menuOrder 는 메뉴 안 순서입니다(표의 `_menuOrder` 와 같은 뜻 — 백의 자리가 바뀌면 구분선).
 * @code SW_EDITOR_COMMAND( ParkReload, "themepark.reloadLayout", 9100, "Reload Park Layout", editoricon::kRefresh, "ThemePark", "배치 파일을 다시 읽습니다",
 *                          "Reload rides.xml", {}, &reloadLayout, nullptr, "MainMenu/ThemePark" ); @endcode
 */
#define SW_EDITOR_COMMAND( name, pID, menuOrder, pLabel, pIcon, pCategory, pTooltip, pDetail, shortcut, pfnAction, pfnEnabled, pMenuPath )        \
    SW_EDITOR_REGISTER( ::sw::editor::EditorCommandRegistration, Command_##name, { pID, menuOrder }, pLabel, pIcon, pCategory, pTooltip, pDetail, \
                        shortcut, pfnAction, pfnEnabled, pMenuPath )
