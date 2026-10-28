/**
 * @file EditorCommandRegistry.h
 * @brief 에디터 커맨드의 단일 정의(SSOT)입니다. 메뉴 · 전역 단축키 · 커맨드 팔레트가 같은 정의를 읽습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
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
        string _id;       ///< "scene.save" 처럼 점으로 구분한 고유 id
        string _label;    ///< 메뉴와 팔레트에 보이는 이름
        string _icon;     ///< 메뉴 라벨 앞에 붙는 아이콘 글리프. 비어도 된다
        string _category; ///< 팔레트 그룹 이름
        string _tooltip;  ///< 메뉴 항목 툴팁(한국어)
        string _detail;   ///< 팔레트 오른쪽 설명(영어)
        /**
         * @brief 이 커맨드를 부르는 에디터 입력 맵(`editor.input.xml`)의 액션 이름입니다. 비어 있으면 단축키가 없습니다.
         * @details 키 · 수정 키 · 범위(레이어) · 사용자 재배치는 입력 맵이 정한다 — 명령은 액션 이름만 든다(EditorUX 10-4). 액션 이름은 관례로 명령 id 와 같다.
         */
        string _inputAction;
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
        /**
         * @brief 바인딩되어 있으면 이 커맨드는 맥락 메뉴에서 실행 항목이 아니라 하위 메뉴(라벨이 이름)이고, 이 함수가 그 안을 그립니다.
         * @details "Add Component" 의 검색 목록, "Select All With" 의 종류 · 태그 목록처럼 대상에 따라 항목이 바뀌는 메뉴다. 그리는 함수는 어댑터(`EditorInputGUI`)가
         *          하위 메뉴를 연 동안만 부른다. 이 칸의 형식은 ImGui 를 모른다(그리는 쪽이 안다).
         */
        Delegate<void( const EditorCommandTarget& )> _submenu;
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
         * @brief 중복 id, 한 액션을 두 커맨드가 부르는 것, 같은 메뉴 · 맥락 메뉴의 같은 순서, 어디서도 그리지 않는 메뉴 경로를 검사해 outReport 에 적습니다. 문제가 없으면 true 입니다.
         * @details 키 조합의 겹침은 입력 맵이 레이어 안에서 본다(`InputMap::findSameLayerInputConflict`). 여기서는 한 누름이 두 명령을 부르는 액션 공유를 잡는다.
         */
        bool validate( string& outReport ) const;

        /** @brief 아이콘과 라벨을 합친 메뉴 표시 문자열을 만듭니다. */
        static void formatMenuLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer128>& outLabel );

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

        const utf8*             _pLabel;
        const utf8*             _pIcon;
        const utf8*             _pCategory;
        const utf8*             _pTooltip;
        const utf8*             _pDetail;
        const utf8*             _pInputAction;     ///< 에디터 입력 맵의 액션 이름. nullptr 이면 단축키가 없다(`EditorCommandDesc::_inputAction`)
        EditorCommandActionFn   _pfnAction;        ///< 대상을 받는 동작(등록 줄이 인자 없는 함수를 `EditorCommandTargetUtil::toAction` 으로 맞춘다)
        EditorCommandEnabledFn  _pfnEnabled;       ///< nullptr 이면 늘 켜져 있다
        EditorCommandEdit       _edit;             ///< 씬을 바꾸는지(필수 — `EditorCommandEdit`)
        const utf8*             _pMenuPath;        ///< nullptr 이면 메뉴에 나오지 않는다(팔레트와 단축키만 쓴다)
        EditorCommandTargetKind _contextMenuKind;  ///< 놓일 맥락 메뉴(필수 — `None` 이면 맥락 메뉴에 없다)
        int32                   _contextMenuOrder; ///< 맥락 메뉴 안 순서
        EditorCommandActionFn   _pfnSubmenu;       ///< nullptr 이 아니면 맥락 메뉴의 하위 메뉴를 그린다(`EditorCommandDesc::_submenu`)
        bool                    _bPaletteVisible;  ///< 팔레트에 나오는지. 대상이 있어야 뜻이 있는 맥락 메뉴 전용 명령은 false
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
         * @brief 등록 항목을 모두 @p registry 에 추가합니다. 항목이 [@p pExcludeBegin, @p pExcludeEnd)(언로드되는 모듈 라이브러리) 안에 있으면 건너뜁니다.
         * @return 건너뛴 항목 수입니다.
         */
        static uint32 appendRegistrations( EditorCommandRegistry& registry, const void* pExcludeBegin, const void* pExcludeEnd );
    };
} // namespace sw::editor

/**
 * @brief 커맨드 하나를 그 커맨드의 .cpp 에서 등록합니다. @p menuOrder 는 메뉴 안 순서이며 테이블의 `_menuOrder` 와 같은 뜻입니다. 백의 자리가 바뀌면 구분선이 들어갑니다.
 *        @p pInputAction 은 에디터 입력 맵의 액션 이름(단축키가 없으면 nullptr)이고, 키 조합은 그 맵(`editor.input.xml`, 확장은 `SW_EDITOR_INPUT_MAP` 로 더한 파일)이 정한다.
 *        @p contextMenu 는 놓일 맥락 메뉴(`EditorCommandTargetKind` 열거자 이름, 없으면 `None`)이고 @p contextMenuOrder 는 그 안 순서다.
 * @code SW_EDITOR_COMMAND( ParkReload, "themepark.reloadLayout", 9100, "Reload Park Layout", editoricon::kRefresh, "ThemePark", "배치 파일을 다시 읽습니다",
 *                          "Reload rides.xml", "themepark.reloadLayout", &reloadLayout, nullptr, NoScene, "MainMenu/ThemePark", None, 0 ); @endcode
 */
#define SW_EDITOR_COMMAND( name, pID, menuOrder, pLabel, pIcon, pCategory, pTooltip, pDetail, pInputAction, pfnAction, pfnEnabled, edit, pMenuPath, contextMenu, \
                           contextMenuOrder )                                                                                                                    \
    SW_EDITOR_REGISTER( ::sw::editor::EditorCommandRegistration, Command_##name, { pID, menuOrder }, pLabel, pIcon, pCategory, pTooltip, pDetail,                \
                        pInputAction, ::sw::editor::EditorCommandTargetUtil::toAction<pfnAction>(),                                                              \
                        ::sw::editor::EditorCommandTargetUtil::toEnabled<pfnEnabled>(), ::sw::editor::EditorCommandEdit::edit, pMenuPath,                        \
                        ::sw::editor::EditorCommandTargetKind::contextMenu, contextMenuOrder, nullptr, true )

/**
 * @brief 맥락 메뉴에만 나오는 커맨드(대상이 있어야 뜻이 있다 — 메인 메뉴 · 팔레트에 없음)를 등록합니다. @p pfnAction · @p pfnEnabled 는 대상(`EditorCommandTarget`)을 받습니다.
 *        @p pfnSubmenu 가 nullptr 이 아니면 그 항목은 하위 메뉴이고 그 함수가 안을 그립니다(그때 @p pfnAction 은 nullptr).
 * @code SW_EDITOR_CONTEXT_COMMAND( ObjectRename, "scene.renameObject", "Rename", editoricon::kEdit, "Scene", GameObject, 120, "hierarchy.rename",
 *                                  &renameTarget, &isSingleTarget, NoScene, nullptr ); @endcode
 */
#define SW_EDITOR_CONTEXT_COMMAND( name, pID, pLabel, pIcon, pCategory, contextMenu, contextMenuOrder, pInputAction, pfnAction, pfnEnabled, edit, pfnSubmenu ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorCommandRegistration, Command_##name, { pID, 0 }, pLabel, pIcon, pCategory, nullptr, nullptr, pInputAction,         \
                        ::sw::editor::EditorCommandTargetUtil::toAction<pfnAction>(), ::sw::editor::EditorCommandTargetUtil::toEnabled<pfnEnabled>(),          \
                        ::sw::editor::EditorCommandEdit::edit, nullptr, ::sw::editor::EditorCommandTargetKind::contextMenu, contextMenuOrder,                  \
                        ::sw::editor::EditorCommandTargetUtil::toAction<pfnSubmenu>(), false )
