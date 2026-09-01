/**
 * @file EditorService.h
 * @brief 에디터 모듈 내부에서 사용하는 C++ 서비스 로케이터 및 작업공간 상태 관리.
 */
#pragma once
#include "RuntimeAPI/Service/ModuleService.h"

namespace sw::editor
{
	struct EditorData;

	namespace internal
	{
		void* getRawService( ModuleServiceId id );
	}

	void bindEditorService( const ModuleService& service );
	void unbindEditorService();

	template <typename T>
	T* getService()
	{
		return static_cast<T*>( internal::getRawService( ModuleServiceTraits<T>::id ) );
	}

	EditorData& getEditorData();
	void		setEditorData( EditorData* pData );
} // namespace sw::editor
