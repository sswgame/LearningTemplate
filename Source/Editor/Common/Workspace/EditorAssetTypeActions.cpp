#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetTypeActions.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAssetTypeActionsInternal
        {
            /** @brief 종류 → 동작 목록입니다. 함수 안 정적 변수라 다른 번역 단위의 등록자보다 늦게 만들어질 걱정이 없습니다. 이름이 곧 종류 값입니다. */
            static RegistrationList<const IEditorAssetTypeActions>& getList()
            {
                static RegistrationList<const IEditorAssetTypeActions> s_list{ "EditorAssetTypeActionsRegistry", RegistrationOrder::Insertion, true };
                return s_list;
            }

            /** @brief 종류의 목록 이름(10 진수)을 @p outBuffer 에 씁니다. `Unknown` · 범위 밖이면 false 입니다. */
            static bool makeKey( EditorAssetType kind, utf8 ( &outBuffer )[16] )
            {
                const uint32 index = static_cast<uint32>( kind );
                if ( index == 0 || index >= static_cast<uint32>( EditorAssetType::Count ) )
                    return false;
                (void)std::snprintf( outBuffer, sizeof( outBuffer ), "%u", index );
                return true;
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
        utf8 arrKey[16]{};
        if ( EditorAssetTypeActionsInternal::makeKey( actions.getKind(), arrKey ) == false )
        {
            SW_LOG_ERROR( "Asset type actions registered for an invalid kind (%#)", static_cast<uint32>( actions.getKind() ) );
            return;
        }
        if ( EditorAssetTypeActionsInternal::getList().add( &actions, arrKey ) == RegistrationResult::DuplicateName )
            SW_LOG_ERROR( "Asset type actions for kind %# are already registered - keeping the first", static_cast<uint32>( actions.getKind() ) );
    }

    void EditorAssetTypeActionsRegistry::unregisterActions( const IEditorAssetTypeActions& actions )
    {
        (void)EditorAssetTypeActionsInternal::getList().remove( &actions );
    }

    const IEditorAssetTypeActions* EditorAssetTypeActionsRegistry::findActions( EditorAssetType kind )
    {
        utf8 arrKey[16]{};
        if ( EditorAssetTypeActionsInternal::makeKey( kind, arrKey ) == false )
            return nullptr;
        return EditorAssetTypeActionsInternal::getList().findByName( arrKey );
    }

    const IEditorAssetTypeActions* EditorAssetTypeActionsRegistry::findActionsForPath( string_view path )
    {
        return findActions( EditorAssetTypeRegistry::findKind( path ) );
    }
} // namespace sw::editor
