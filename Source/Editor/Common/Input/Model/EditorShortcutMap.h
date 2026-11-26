/**
 * @file EditorShortcutMap.h
 * @brief 에디터 단축키의 Model — 에디터 입력 맵(`editor.input.xml` + 확장의 맵)의 소유 · 다시 만들기 · 포커스 레이어 · 갱신 · 발화 판정 · 사용자 재배치입니다. ImGui 를 모릅니다.
 * @details 어댑터(`EditorInputActions` — ImGui 키 넘기기 · 키보드 주인 · 글 입력 중 막기 · 메뉴 명령 줄)가 이것을 값으로 들고 UI 프레임 첫머리에 부른다.
 *          언리얼 `FInputBindingManager`(바인딩 · 사용자 재배치)와 `FUICommandList`(발화 → 명령)의 선이다. 단위 테스트는 지역 입력 매니저로 부른다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/Name/hashed_string.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorExtensionRegistry.h"

#include "Engine/Input/Map/InputMap.h"

namespace sw
{
    class InputManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorInputMapRegistration
     * @brief 확장 모듈이 에디터 입력 맵에 자기 액션 · 레이어를 더하는 등록 줄입니다(`SW_EDITOR_INPUT_MAP`).
     * @details 파일은 `InputMap::appendFromAsset` 규칙을 따른다 — 맵 전체 속성을 적지 않고, 있는 액션 · 레이어를 다시 정의하지 않고,
     *          같은 레이어의 같은 입력을 다른 액션과 나누지 않는다. 확장 명령의 `SW_EDITOR_COMMAND` 입력 액션 칸이 이 파일의 액션을 가리킨다.
     */
    struct EditorInputMapRegistration : EditorExtensionRegistration
    {
        SW_EDITOR_EXTENSION_KIND( "inputMap" );

        const utf8* _pAssetPath; ///< 리소스 경로(`editor/input/…` 또는 확장 팩의 경로)
    };
} // namespace sw::editor

namespace sw::editor
{
    struct EditorCommandDesc;

    /**
     * @class EditorShortcutMap
     * @brief 에디터 입력 맵과 그 판정입니다. 파일 머리말 참고.
     * @details 레이어 규칙: `Editor.Global` 은 늘 켜져 있고, `Editor.Panel.<패널 id>` 는 그 패널 창이 포커스일 때만 켜진다(`setFocusedPanel`).
     *          같은 누름으로 포커스 패널 레이어의 액션과 전역 액션이 함께 발화하면 패널 쪽만 실행한다(`collectTriggeredCommands`).
     */
    class SW_EDITOR_API EditorShortcutMap
    {
    public:
        /** @brief 늘 켜진 전역 레이어 이름입니다. */
        static constexpr const utf8* kGlobalLayer = "Editor.Global";
        /** @brief 패널 레이어 이름의 앞부분입니다. 레이어 이름은 이 뒤에 패널 id(`EditorPanelEntry::_id`)를 붙인 것입니다. */
        static constexpr const utf8* kPanelLayerPrefix = "Editor.Panel.";
        /** @brief 사용자 재배치 파일 이름입니다(에디터 상태 폴더 — `Saved/Editor`). 형식은 디버그 키 맵과 같다(`DebugKeyActions::saveUserBindings`). */
        static constexpr const utf8* kUserBindingsFileName = "editorbindings.xml";

        EditorShortcutMap();

        /** @brief 맵을 @p input 에 붙이고 기본 맵 · 확장 맵 · 사용자 재배치를 읽습니다. 못 읽으면 끊고 false 입니다(단축키 없는 에디터로 서지 않는다). */
        [[nodiscard]] bool connect( InputManager& input );
        /** @brief 맵을 입력 매니저에서 끊습니다(끊긴 맵의 `update` 는 아무것도 하지 않는다). */
        void disconnect();
        /** @brief 확장 모듈이 맵을 더하거나 뺐으면 처음부터 다시 만듭니다 — 빠진 모듈의 액션이 남지 않고, 다시 올린 모듈의 액션이 두 번 더해지지 않는다. */
        void rebuildIfExtensionsChanged();
        /** @brief 포커스 패널 @p panelID(비면 없음)의 레이어만 켭니다. 그 패널에 레이어가 없으면 패널 레이어를 모두 끈다. 바뀌었을 때만 레이어를 건드립니다. */
        void setFocusedPanel( string_view panelID );
        /** @brief 맵을 한 프레임 갱신합니다(입력 매니저의 `beginFrame` 뒤). */
        void update( float32 deltaSeconds );
        /**
         * @brief 이번 프레임에 발화한 액션의 명령 id 를 @p outListCommandID 에 담습니다. 포커스 패널 레이어의 명령이 있으면 그것만 담는다(포커스 창 → 전역).
         * @details 한 액션이 여러 패널 레이어에 바인딩을 가질 수 있다(Delete 가 계층 창 · 씬 뷰에 각자) — 포커스 레이어의 바인딩이 하나라도 있으면 패널 쪽이다.
         */
        void collectTriggeredCommands( const vector<EditorCommandDesc>& listCommand, vector<string>& outListCommandID ) const;

        /**
         * @brief 단축키 데이터와 코드를 맞춰 봅니다. 입력 맵 액션 가운데 명령의 액션도 코드 액션(`editorinput::kArrCodeAction` · 북마크)도 아닌 것을 @p outListUnknown 에,
         *        코드 액션 가운데 입력 맵에 없는 것을 @p outListMissing 에 담습니다(둘 다 비우고 채운다). 오타 · 지운 명령의 남은 줄은 조용히 없는 단축키가 된다.
         */
        static void collectActionMismatches( const vector<hashed_string>& listMapAction, const vector<EditorCommandDesc>& listCommand, vector<string>& outListUnknown,
                                             vector<string>& outListMissing );

        /** @brief 액션이 이번 프레임에 발화했으면 true 입니다(막기는 어댑터가 본다). */
        bool wasActionTriggered( string_view action ) const;
        /** @brief 액션이 눌려 있으면 true 입니다. */
        bool isActionDown( string_view action ) const;
        /** @brief 몸짓 동안만 켜는 모드 레이어를 켜고 끕니다(뷰포트 비행). 맵에 없는 레이어면 부른 쪽의 잘못이라 멈춥니다. */
        void setModeLayerEnabled( string_view layer, bool bEnabled );

        /** @brief 에디터 입력 맵입니다(값으로 소유 — 늘 있다). */
        InputMap&       getInputMap() { return _map; }
        const InputMap& getInputMap() const { return _map; }
        /** @brief 지금 켜진 패널 레이어 이름입니다. 포커스 패널에 레이어가 없으면 빈 이름입니다. */
        const hashed_string& getFocusedPanelLayer() const { return _focusedPanelLayer; }

        // ------------------------------------------------------------------------------
        // 단축키 글자 · 재배치(단축키 창이 쓴다). 바꾼 것은 곧바로 사용자 재배치 파일에 저장한다
        // ------------------------------------------------------------------------------
        /** @brief 액션의 키보드 바인딩을 메뉴 글자(`Ctrl+Shift+P / Ctrl+Space`)로 만듭니다. 액션이 없거나 빈 이름이면 빈 글입니다. */
        void formatShortcutLabel( string_view action, string& outLabel ) const;
        /** @brief 액션의 첫 키보드 바인딩을 @p key + @p modifierMask 로 바꿉니다. 같은 레이어의 다른 액션과 겹치면 바꾸지 않고 그 이름을 @p outConflictAction 에 담고 false 입니다. */
        [[nodiscard]] bool rebindShortcut( string_view action, Key key, uint8 modifierMask, hashed_string& outConflictAction );
        /** @brief @p action 을 @p key + @p modifierMask 로 바꾸고, 겹치던 @p otherAction 에는 @p action 의 옛 조합을 줍니다(바꾸기). 저장합니다. */
        [[nodiscard]] bool swapShortcut( string_view action, string_view otherAction, Key key, uint8 modifierMask );
        /** @brief 액션의 바인딩을 기본으로 되돌리고 저장합니다. */
        void resetShortcut( string_view action );
        /** @brief 모든 바인딩을 기본으로 되돌리고 저장합니다(재배치 파일은 지워진다). */
        void resetAllShortcuts();
        /** @brief 액션의 첫 바인딩이 기본과 다르면 true 입니다. */
        bool isShortcutModified( string_view action ) const;
        /** @brief 기본과 다른 액션 수입니다(감시 값 `Editor.ShortcutOverrideCount`). */
        uint32 countModifiedShortcuts() const;
        /** @brief 사용자 재배치 파일 경로입니다(`Saved/Editor/editorbindings.xml`). */
        static AbsolutePath getUserBindingsPath();

    private:
        /** @brief 기본 맵을 다시 읽고, 확장 맵을 더하고, 같은 레이어 겹침을 검사하고, 사용자 재배치를 입힙니다. 실패하면 false 입니다. */
        [[nodiscard]] bool rebuildMap();
        /** @brief 액션의 첫 키보드 바인딩 번호를 찾습니다. 없으면 false 입니다. */
        [[nodiscard]] bool findKeyboardBinding( const hashed_string& action, uint32& outBindIndex ) const;
        /** @brief 사용자 재배치 파일을 씁니다. 실패는 경고로 남깁니다. */
        void saveUserBindings() const;

    private:
        InputMap      _map;
        hashed_string _focusedPanelLayer;   ///< 지금 켠 패널 레이어(없으면 빈 이름)
        uint32        _syncedMapGeneration; ///< 마지막으로 더한 확장 맵 등록 세대
    };
} // namespace sw::editor

/**
 * @brief 확장 모듈이 에디터 입력 맵에 자기 액션 파일을 더합니다. @p pID 는 등록 id, @p pAssetPath 는 `*.input.xml` 리소스 경로입니다.
 * @code SW_EDITOR_INPUT_MAP( ParkInput, "themepark.input", "editor/input/themepark.input.xml" ); @endcode
 */
#define SW_EDITOR_INPUT_MAP( name, pID, pAssetPath ) SW_EDITOR_REGISTER( ::sw::editor::EditorInputMapRegistration, InputMap_##name, { pID, 0 }, pAssetPath )
