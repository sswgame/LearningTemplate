/**
 * @file EditorCommandRegistry.h
 * @brief 에디터 커맨드 SSOT — 메뉴·전역 단축키·커맨드 팔레트가 같은 정의를 읽습니다
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/fixed_string.h"

namespace sw::editor
{
    /**
     * @brief 단축키 키 코드.
     * @details ImGui 헤더에 묶이지 않도록 자체 열거형을 씁니다 — 이 파일은 ImGui 없이 컴파일되어야
     *          단위 테스트가 붙습니다. ImGuiKey 변환은 유일한 소비자(`EditorCommandGui`)가 합니다.
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
        Count
    };

    /** @brief 단축키 수정자 비트 */
    namespace commandmod
    {
        inline constexpr uint8 kNone  = 0;
        inline constexpr uint8 kCtrl  = static_cast<uint8>( SW_BIT( 0 ) );
        inline constexpr uint8 kShift = static_cast<uint8>( SW_BIT( 1 ) );
        inline constexpr uint8 kAlt   = static_cast<uint8>( SW_BIT( 2 ) );
        /**
         * @brief 라벨에만 보이고 우리가 처리하지 않는 조합입니다.
         * @details Alt+F4 처럼 OS 가 먼저 가로채는 것을 메뉴에 적어 두기 위한 표시입니다. 이 비트가
         *          있으면 단축키 처리기가 건너뜁니다 — 처리하는 척하는 라벨이 남지 않게 합니다.
         */
        inline constexpr uint8 kDisplayOnly = static_cast<uint8>( SW_BIT( 3 ) );
    } // namespace commandmod

    /** @brief 키 조합 하나 */
    struct EditorCommandShortcut
    {
        EditorCommandKey _key{ EditorCommandKey::None };
        uint8            _modifier{ commandmod::kNone };
    };

    /**
     * @brief 에디터 커맨드 한 개의 정의.
     * @details 한 커맨드는 최대 세 표면(메뉴바·전역 단축키·커맨드 팔레트)에 나타납니다. 예전에는
     *          표면마다 따로 적혀 있어서 서로 어긋났습니다 — 그래서 정의는 여기 한 번만 둡니다.
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
        EditorCommandShortcut _altShortcut; ///< 같은 커맨드의 두 번째 조합. 없으면 None
        Delegate<void()>      _action;
        Delegate<bool()>      _enabledPredicate; ///< 바인딩되지 않으면 항상 활성입니다
        bool                  _bPaletteVisible{ true };
    };

    /**
     * @class EditorCommandRegistry
     * @brief 에디터 커맨드 정의를 모아 두고 id 로 찾아 실행합니다 (EditorContext 소유).
     */
    class EditorCommandRegistry
    {
    public:
        EditorCommandRegistry()  = default;
        ~EditorCommandRegistry() = default;

        /** @brief 등록된 커맨드를 모두 버립니다. */
        void clear();
        /** @brief 커맨드를 등록합니다. id 가 비어 있으면 무시합니다. */
        void registerCommand( EditorCommandDesc desc );

        /** @brief id 로 커맨드를 찾습니다. 없으면 nullptr입니다. */
        const EditorCommandDesc* find( string_view commandId ) const;
        /** @brief 활성 조건을 평가합니다. 조건이 없으면 true입니다. */
        static bool isEnabled( const EditorCommandDesc& desc );
        /** @brief 활성 상태이면 실행하고 true입니다. */
        bool execute( string_view commandId );
        /** @brief 활성 상태이면 실행하고 true입니다. */
        static bool executeDesc( const EditorCommandDesc& desc );

        const vector<EditorCommandDesc>& getCommands() const { return _listCommand; }

        /**
         * @brief 중복 id·중복 키 조합을 outReport 에 적습니다. 문제가 없으면 true입니다.
         * @details 표면이 셋으로 갈려 있던 동안 같은 조합을 두 곳이 처리하는 일이 실제로 있었습니다
         *          (Ctrl+Z 가 전역과 Inspector 에서 각각 undo 를 불러 두 번 되돌렸습니다). 정의를
         *          한곳으로 모은 다음에는 그런 충돌을 기계가 잡을 수 있습니다.
         */
        bool validate( string& outReport ) const;

        /** @brief "Ctrl+Shift+B" 또는 "Ctrl+Alt+F11 / F7" 형태의 단축키 라벨을 씁니다. */
        static void formatShortcutLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer64>& outLabel );
        /** @brief 아이콘과 라벨을 합친 메뉴 표시 문자열을 씁니다. */
        static void formatMenuLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer128>& outLabel );
        /** @brief 키 하나의 표시 이름입니다. None 이면 빈 문자열입니다. */
        static const utf8* getKeyName( EditorCommandKey key );
        /** @brief 두 조합이 같은 키·같은 수정자이면 true입니다. */
        static bool isSameShortcut( const EditorCommandShortcut& lhs, const EditorCommandShortcut& rhs );
        /** @brief 우리가 실제로 처리하는 조합이면 true입니다 (키가 있고 DisplayOnly 가 아닙니다). */
        static bool isHandledShortcut( const EditorCommandShortcut& shortcut );

    private:
        vector<EditorCommandDesc> _listCommand;
    };
} // namespace sw::editor
