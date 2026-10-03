#include "pch.h"

#include "Editor/AssetActions/EditorAssetTypeActions.h"

#include "Core/Container/array.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAssetTypeActionsInternal
        {
            using ActionsTable = array<const IEditorAssetTypeActions*, static_cast<size_t>( EditorAssetKind::Count )>;

            /** @brief 종류 값으로 찾는 표입니다. 함수 안 정적 변수라 다른 번역 단위의 등록자보다 늦게 만들어질 걱정이 없습니다. */
            static ActionsTable& getTable()
            {
                static ActionsTable s_arrActions{};
                return s_arrActions;
            }

            /** @brief 표의 칸입니다. `Unknown` · 범위 밖이면 nullptr 입니다. */
            static const IEditorAssetTypeActions** findSlot( EditorAssetKind kind )
            {
                const size_t index = static_cast<size_t>( kind );
                if ( index == 0 || index >= getTable().size() )
                    return nullptr;
                return &getTable()[index];
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

    bool IEditorAssetTypeActions::open( string_view /*relativePath*/ ) const { return false; }

    bool IEditorAssetTypeActions::dropInViewport( GameObjectManager* /*pManager*/, const utf8* /*pPath*/, const float3& /*spawnPos*/ ) const
    {
        return false;
    }

    void EditorAssetTypeActionsRegistry::registerActions( const IEditorAssetTypeActions& actions )
    {
        const IEditorAssetTypeActions** ppSlot = EditorAssetTypeActionsInternal::findSlot( actions.getKind() );
        if ( ppSlot == nullptr )
        {
            SW_LOG_ERROR( "Asset type actions registered for an invalid kind (%#)", static_cast<uint32>( actions.getKind() ) );
            return;
        }
        if ( *ppSlot != nullptr && *ppSlot != &actions )
        {
            SW_LOG_ERROR( "Asset type actions for kind %# are already registered - keeping the first", static_cast<uint32>( actions.getKind() ) );
            return;
        }
        *ppSlot = &actions;
    }

    void EditorAssetTypeActionsRegistry::unregisterActions( const IEditorAssetTypeActions& actions )
    {
        const IEditorAssetTypeActions** ppSlot = EditorAssetTypeActionsInternal::findSlot( actions.getKind() );
        if ( ppSlot != nullptr && *ppSlot == &actions )
            *ppSlot = nullptr;
    }

    const IEditorAssetTypeActions* EditorAssetTypeActionsRegistry::findActions( EditorAssetKind kind )
    {
        const IEditorAssetTypeActions** ppSlot = EditorAssetTypeActionsInternal::findSlot( kind );
        return ppSlot != nullptr ? *ppSlot : nullptr;
    }

    const IEditorAssetTypeActions* EditorAssetTypeActionsRegistry::findActionsForPath( string_view path )
    {
        return findActions( EditorAssetTypeRegistry::findKind( path ) );
    }
} // namespace sw::editor
