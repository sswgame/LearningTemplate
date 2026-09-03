#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Core/Container/map.h"

SW_LOG_CALLER( "EditorService" );
namespace sw::editor
{
    namespace
    {
        ModuleService      s_editorService{};
        map<uint64, void*> s_mapLocalService{};
        EditorData*        s_pEditorData{ nullptr };
    } // namespace

    void bindEditorService( const ModuleService& service )
    {
        s_editorService = service;
    }

    void unbindEditorService()
    {
        s_editorService = {};
        s_mapLocalService.clear();
    }

    namespace internal
    {
        void* getRawService( sw::internal::ModuleServiceId id )
        {
            const uint32 rawId = sw::internal::toRawServiceId( id );
            if ( rawId >= sw::internal::kModuleServiceCount )
                return nullptr;
            return const_cast<void*>( s_editorService.arrServices[rawId] );
        }

        void bindRawLocalService( uint64 typeHash, void* pService )
        {
            if ( pService != nullptr )
                s_mapLocalService[typeHash] = pService;
            else
                s_mapLocalService.erase( typeHash );
        }

        void* getRawLocalService( uint64 typeHash )
        {
            const auto it = s_mapLocalService.find( typeHash );
            return it != s_mapLocalService.end() ? it->second : nullptr;
        }
    } // namespace internal

    EditorData& getEditorData()
    {
        SW_LOG_ASSERT( s_pEditorData != nullptr, "EditorData is not bound" );
        return *s_pEditorData;
    }

    void setEditorData( EditorData* pData )
    {
        s_pEditorData = pData;
    }
} // namespace sw::editor
