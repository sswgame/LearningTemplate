/**
 * @file InspectorPropertyRegistry.h
 * @brief 프로퍼티 타입명 → IInspectorProperty
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
	class IInspectorProperty;

	/** @brief 타입별 인스펙터 프로퍼티 UI 레지스트리 */
	class InspectorPropertyRegistry
	{
	public:
		InspectorPropertyRegistry()	 = default;
		~InspectorPropertyRegistry() = default;

		static void					registerType( string_view typeName, unique_ptr<IInspectorProperty> pProperty );
		static IInspectorProperty* find( string_view typeName );
		static void					registerDefaults();

		void				   registerTypeImpl( string_view typeName, unique_ptr<IInspectorProperty> pProperty );
		IInspectorProperty*	   findImpl( string_view typeName ) const;
		void				   registerDefaultsImpl();

	private:
		map<string, unique_ptr<IInspectorProperty>> _mapProperties;
	};
} // namespace sw::editor
