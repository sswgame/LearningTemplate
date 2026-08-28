/**
 * @file InspectorPropertyManager.h
 * @brief 프로퍼티 타입명 → IInspectorProperty (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
	class IInspectorProperty;

	/** @brief 타입별 인스펙터 프로퍼티 UI 관리자 (EditorContext 소유) */
	class InspectorPropertyManager
	{
	public:
		InspectorPropertyManager()	= default;
		~InspectorPropertyManager() = default;

		void				registerType( string_view typeName, unique_ptr<IInspectorProperty> pProperty );
		IInspectorProperty* find( string_view typeName ) const;
		void				registerDefaults();

	private:
		map<string, unique_ptr<IInspectorProperty>> _mapProperty;
	};
} // namespace sw::editor
