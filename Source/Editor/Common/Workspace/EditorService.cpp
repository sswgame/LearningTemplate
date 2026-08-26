#include "pch.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	namespace
	{
		ModuleService	   s_editorService{};
		EditorData*		   s_pEditorData{ nullptr };
	} // namespace

	void bindEditorService( const ModuleService& service )
	{
		s_editorService = service;
	}

	void unbindEditorService()
	{
		s_editorService = {};
	}

	void* getRawService( uint32 id )
	{
		SW_LOG_ASSERT( s_editorService.getService != nullptr, "EditorService is not bound" );
		return s_editorService.getService( id );
	}

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
