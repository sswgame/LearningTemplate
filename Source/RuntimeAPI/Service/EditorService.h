/**
 * @file EditorService.h
 * @brief App이 채우고 에디터 모듈이 쓰는 서비스 로케이터.
 * @note 게임 모듈은 이 헤더를 포함하지 않습니다. 에디터 전용 id는 ModuleServiceId::Count 이후입니다.
 */
#pragma once

#if !defined( SW_EDITOR_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL )
	#error "EditorService.h can only be included by Editor or App."
#endif

#include "RuntimeAPI/Service/ModuleService.h"

namespace sw
{
	class TaskManager;
	class MemoryProfiler;
	class CommandStack;
	class IModuleCompiler;
	struct EngineData;

	enum class EditorServiceId : uint32
	{
		TaskManager = kModuleServiceCount,
		MemoryProfiler,
		CommandStack,
		EngineData,
		ModuleCompiler
	};

	inline constexpr uint32 toRawServiceId( EditorServiceId id )
	{
		return static_cast<uint32>( id );
	}

	template <typename T>
	struct EditorServiceTraits
	{
		static constexpr uint32 id = toRawServiceId( ModuleServiceTraits<T>::id );
	};

#define SW_DECLARE_EDITOR_SERVICE( Type, Id )              \
	template <>                                            \
	struct EditorServiceTraits<Type>                       \
	{                                                      \
		static constexpr uint32 id = toRawServiceId( Id ); \
	}

	SW_DECLARE_EDITOR_SERVICE( TaskManager, EditorServiceId::TaskManager );
	SW_DECLARE_EDITOR_SERVICE( MemoryProfiler, EditorServiceId::MemoryProfiler );
	SW_DECLARE_EDITOR_SERVICE( CommandStack, EditorServiceId::CommandStack );
	SW_DECLARE_EDITOR_SERVICE( const EngineData, EditorServiceId::EngineData );
	SW_DECLARE_EDITOR_SERVICE( IModuleCompiler, EditorServiceId::ModuleCompiler );

	namespace editor
	{
		struct EditorData;

		SW_MODULE_API void bindEditorService( const ModuleService& service );
		SW_MODULE_API void unbindEditorService();

		SW_MODULE_API void* getRawService( uint32 id );

		template <typename T>
		T* getService()
		{
			return static_cast<T*>( getRawService( EditorServiceTraits<T>::id ) );
		}

		SW_MODULE_API EditorData& getEditorData();
		SW_MODULE_API void		  setEditorData( EditorData* pData );
	} // namespace editor
} // namespace sw
