#pragma once

#if !defined( SW_EDITOR_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL )
	#error "EditorService.h can only be included by Editor or App."
#endif
#include "Core/Common/Macros.h"

namespace sw
{
	class LocalizationManager;
	class TaskManager;
	class SceneManager;
	class GlobalVariableManager;
	class ResourceManager;
	class TypeRegistry;
	class MemoryProfiler;
	struct EditorUIContext;

	class CommandStack;
	struct DebugOverlayState;
	class DebugDrawQueue;
	struct EditorData;
	struct EngineData;

	enum class EditorServiceId : uint32
	{
		LocalizationManager = 0,
		TaskManager,
		SceneManager,
		GlobalVariableManager,
		ResourceManager,
		TypeRegistry,
		MemoryProfiler,
		UIContext,

		CommandStack,
		DebugOverlayState,
		DebugDrawQueue,
		EngineData,
	};

	struct EditorService
	{
		void* ( *getService )(EditorServiceId id){ nullptr };
	};

	template <typename T>
	struct EditorServiceTraits;

#define SW_DECLARE_EDITOR_SERVICE( Type, Id )     \
	template <>                                   \
	struct EditorServiceTraits<Type>              \
	{                                             \
		static constexpr EditorServiceId id = Id; \
	}

	SW_DECLARE_EDITOR_SERVICE( LocalizationManager, EditorServiceId::LocalizationManager );
	SW_DECLARE_EDITOR_SERVICE( TaskManager, EditorServiceId::TaskManager );
	SW_DECLARE_EDITOR_SERVICE( SceneManager, EditorServiceId::SceneManager );
	SW_DECLARE_EDITOR_SERVICE( GlobalVariableManager, EditorServiceId::GlobalVariableManager );
	SW_DECLARE_EDITOR_SERVICE( ResourceManager, EditorServiceId::ResourceManager );
	SW_DECLARE_EDITOR_SERVICE( TypeRegistry, EditorServiceId::TypeRegistry );
	SW_DECLARE_EDITOR_SERVICE( MemoryProfiler, EditorServiceId::MemoryProfiler );
	SW_DECLARE_EDITOR_SERVICE( EditorUIContext, EditorServiceId::UIContext );
	SW_DECLARE_EDITOR_SERVICE( CommandStack, EditorServiceId::CommandStack );
	SW_DECLARE_EDITOR_SERVICE( DebugOverlayState, EditorServiceId::DebugOverlayState );
	SW_DECLARE_EDITOR_SERVICE( DebugDrawQueue, EditorServiceId::DebugDrawQueue );
	SW_DECLARE_EDITOR_SERVICE( const EngineData, EditorServiceId::EngineData );


	namespace editor
	{
		SW_MODULE_API void bindEditorService( const EditorService& service );
		SW_MODULE_API void unbindEditorService();

		SW_MODULE_API void* getRawService( EditorServiceId id );

		template <typename T>
		T* getService()
		{
			return static_cast<T*>( getRawService( EditorServiceTraits<T>::id ) );
		}


		SW_MODULE_API EditorData& getEditorData();
		SW_MODULE_API void		  setEditorData( EditorData* pData );
	} // namespace editor
} // namespace sw
