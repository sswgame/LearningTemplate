#include "pch.h"

#include "RuntimeAPI/EditorService.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Localization/StringTable.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Utility/Resource/ResourceManager.h"

namespace sw
{
	namespace
	{
		EditorService	   s_editorService{};
		struct EditorData* s_pEditorData{ nullptr };
	} // namespace

	namespace editor
	{
		void bindEditorService( const EditorService& service )
		{
			s_editorService = service;
		}

		void unbindEditorService()
		{
			s_editorService = {};
		}

		void* getRawService( EditorServiceId id )
		{
			SW_LOG_ASSERT( s_editorService.getService != nullptr, "EditorService is not bound" );
			return s_editorService.getService( id );
		}

		EditorData& getEditorData()
		{
			SW_LOG_ASSERT( s_pEditorData != nullptr, "EditorData is not bound" );
			return *s_pEditorData;
		}

		void setEditorData( struct EditorData* pData )
		{
			s_pEditorData = pData;
		}
	} // namespace editor
} // namespace sw
