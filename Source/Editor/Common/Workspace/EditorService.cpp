#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"

SW_LOG_CALLER( "EditorService" );
namespace sw::editor
{
    namespace
    {
        ModuleService s_editorService{};
        EditorData*   s_pEditorData{ nullptr };
    } // namespace

    void bindEditorService( const ModuleService& service )
    {
        s_editorService = service;
    }

    void unbindEditorService()
    {
        s_editorService = {};
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
