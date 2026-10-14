/**
 * @file EditorContextMenuExtension.h
 * @brief 하이어라키의 게임 오브젝트 · 콘텐츠 브라우저의 에셋 우클릭 메뉴에 항목 하나를 더하는 등록 줄(`SW_EDITOR_CONTEXT_MENU_ENTRY`)입니다.
 * @details 언리얼의 `UToolMenus` 확장(`LevelEditor.ActorContextMenu` · `ContentBrowser.AssetContextMenu`)과 유니티의 `[MenuItem("GameObject/...")]` ·
 *          `[MenuItem("Assets/...")]` 자리다. 메뉴를 그리는 패널은 항목을 모르고, 자기 종류의 항목을 순서대로 그리기만 한다.
 *          이 헤더는 ImGui 를 쓰지 않는다(EditorTest 가 목록 규칙을 테스트한다).
 */
#pragma once
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @enum EditorContextMenuKind
     * @brief 확장 항목이 붙는 우클릭 메뉴입니다.
     */
    enum class EditorContextMenuKind : uint8
    {
        GameObject, ///< 하이어라키에서 게임 오브젝트 하나를 우클릭한 메뉴. `_pGameObject` · `_pManager` 가 채워진다
        Asset,      ///< 콘텐츠 브라우저에서 파일이나 폴더 하나를 우클릭한 메뉴. `_pAssetPath` · `_bIsDirectory` 가 채워진다
    };

    /**
     * @struct EditorContextMenuTarget
     * @brief 메뉴가 열린 대상입니다. 종류에 맞는 필드만 채워지고 나머지는 비어 있습니다.
     * @details 포인터는 메뉴를 그리는 그 프레임 동안만 유효하다. 항목의 동작은 보관하지 않는다.
     */
    struct EditorContextMenuTarget
    {
        EditorContextMenuKind _kind{ EditorContextMenuKind::GameObject };
        GameObject*           _pGameObject{ nullptr };
        GameObjectManager*    _pManager{ nullptr };
        const utf8*           _pAssetPath{ nullptr }; ///< 절대 경로
        bool                  _bIsDirectory{ false };
    };

    /**
     * @struct EditorContextMenuEntryRegistration
     * @brief 우클릭 메뉴 항목 하나입니다. 패널 고유 항목 아래 구분선 뒤에 `_order` 순서로 붙습니다.
     * @details 문자열은 리터럴이어야 한다. `_pfnEnabled` 가 nullptr 이면 늘 켜져 있다. id 는 종류를 넘어 유일해야 한다(목록이 하나다).
     *          확장 모듈의 항목은 모듈을 언로드할 때 정적 등록자가 빼므로, 메뉴는 매 프레임 목록을 다시 읽는다.
     */
    struct EditorContextMenuEntryRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "contextMenuEntry";

        EditorContextMenuKind _menuKind;
        const utf8*           _pLabel;
        void ( *_pfnAction )( const EditorContextMenuTarget& target );
        bool ( *_pfnEnabled )( const EditorContextMenuTarget& target );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorContextMenuExtensionUtil
     * @brief 한 메뉴 종류의 확장 항목을 고르는 창구입니다.
     */
    struct EditorContextMenuExtensionUtil
    {
        /** @brief @p menuKind 메뉴에 붙는 항목 수를 셉니다. 0 이면 패널은 구분선도 그리지 않습니다. */
        static uint32 countEntries( EditorContextMenuKind menuKind )
        {
            using Registry = EditorExtensionRegistry<EditorContextMenuEntryRegistration>;
            uint32 count   = 0;
            for ( uint32 index = 0; index < Registry::getCount(); ++index )
            {
                if ( Registry::getAt( index )._menuKind == menuKind )
                    ++count;
            }
            return count;
        }

        /** @brief 항목이 @p target 에 대해 켜져 있으면 true 입니다. */
        static bool isEnabled( const EditorContextMenuEntryRegistration& entry, const EditorContextMenuTarget& target )
        {
            return entry._pfnAction != nullptr && ( entry._pfnEnabled == nullptr || entry._pfnEnabled( target ) );
        }

        /**
         * @brief @p menuKind 메뉴의 항목을 `_order` 순서로 하나씩 @p visit 에 넘깁니다. @p visit 가 동작 함수를 고르면(nullptr 이 아니면) 그것을 돌려줍니다.
         * @details 고른 동작은 순회가 끝난 뒤 부르는 쪽이 부른다. 동작이 모듈을 내려 목록이 바뀌어도 순회 중인 배열을 읽지 않는다.
         */
        template <typename TVisit>
        static auto visitEntries( EditorContextMenuKind menuKind, TVisit&& visit ) -> void ( * )( const EditorContextMenuTarget& )
        {
            using Registry = EditorExtensionRegistry<EditorContextMenuEntryRegistration>;
            void ( *pfnChosen )( const EditorContextMenuTarget& ) = nullptr;
            for ( uint32 index = 0; index < Registry::getCount(); ++index )
            {
                const EditorContextMenuEntryRegistration& entry = Registry::getAt( index );
                if ( entry._menuKind != menuKind )
                    continue;
                if ( visit( entry ) && pfnChosen == nullptr )
                    pfnChosen = entry._pfnAction;
            }
            return pfnChosen;
        }
    };
} // namespace sw::editor

/**
 * @code SW_EDITOR_CONTEXT_MENU_ENTRY( ParkRideInfo, "themepark.rideInfo", 9100, ::sw::editor::EditorContextMenuKind::GameObject, "Ride Info",
 *                                     &showRideInfo, &isRide ); @endcode
 */
#define SW_EDITOR_CONTEXT_MENU_ENTRY( name, pID, order, menuKind, pLabel, pfnAction, pfnEnabled )                                       \
    SW_EDITOR_REGISTER( ::sw::editor::EditorContextMenuEntryRegistration, ContextMenuEntry_##name, { pID, order }, menuKind, pLabel, \
                        pfnAction, pfnEnabled )
