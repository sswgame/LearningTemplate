#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetTypeActions.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAssetTypeActionsInternal
        {
            /** @brief 종류 → 동작 목록입니다. 함수 안 정적 변수라 다른 번역 단위의 등록자보다 늦게 만들어질 걱정이 없습니다. 이름은 종류 테이블의 종류 이름입니다. */
            static RegistrationList<const IEditorAssetTypeActions>& getList()
            {
                static RegistrationList<const IEditorAssetTypeActions> s_list{ "EditorAssetTypeActionsRegistry", RegistrationOrder::Insertion, true };
                return s_list;
            }

            /** @brief 종류의 목록 이름입니다. 종류 테이블(`EditorAssetTypeInfo`)의 이름이라, 테이블에 없는 종류(`Unknown` · 범위 밖)는 nullptr 입니다. */
            static const utf8* findKey( EditorAssetType kind )
            {
                const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
                return pInfo == nullptr ? nullptr : pInfo->_pDisplayName;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorAssetTypeActions" );

    bool IEditorAssetTypeActions::drawThumbnail( ImDrawList* /*pDrawList*/, const float2& /*minPos*/, const float2& /*maxPos*/ ) const
    {
        return false;
    }

    bool IEditorAssetTypeActions::open( string_view /*relativePath*/ ) const
    {
        return false;
    }

    bool IEditorAssetTypeActions::dropInViewport( GameObjectManager* /*pManager*/, const utf8* /*pPath*/, const float3& /*spawnPos*/ ) const
    {
        return false;
    }

    void EditorAssetTypeActionsRegistry::registerActions( const IEditorAssetTypeActions& actions )
    {
        const utf8* pKey = EditorAssetTypeActionsInternal::findKey( actions.getKind() );
        if ( pKey == nullptr )
        {
            SW_LOG_ERROR( "Asset type actions registered for an invalid kind (%#)", static_cast<uint32>( actions.getKind() ) );
            return;
        }
        if ( EditorAssetTypeActionsInternal::getList().add( &actions, pKey ) == RegistrationResult::DuplicateName )
            SW_LOG_ERROR( "Asset type actions for kind %# are already registered - keeping the first", static_cast<uint32>( actions.getKind() ) );
    }

    void EditorAssetTypeActionsRegistry::unregisterActions( const IEditorAssetTypeActions& actions )
    {
        (void)EditorAssetTypeActionsInternal::getList().remove( &actions );
    }

    const IEditorAssetTypeActions* EditorAssetTypeActionsRegistry::findActions( EditorAssetType kind )
    {
        const utf8* pKey = EditorAssetTypeActionsInternal::findKey( kind );
        if ( pKey == nullptr )
            return nullptr;
        return EditorAssetTypeActionsInternal::getList().findByName( pKey );
    }

    const IEditorAssetTypeActions* EditorAssetTypeActionsRegistry::findActionsForPath( string_view path )
    {
        return findActions( EditorAssetTypeRegistry::findKind( path ) );
    }
} // namespace sw::editor
