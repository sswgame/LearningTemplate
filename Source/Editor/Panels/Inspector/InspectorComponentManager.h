/**
 * @file InspectorComponentManager.h
 * @brief 컴포넌트 타입명 → IInspectorComponent (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
	class IInspectorComponent;

	/** @brief 컴포넌트 타입별 인스펙터 UI 관리자 (EditorContext 소유) */
	class InspectorComponentManager
	{
	public:
		InspectorComponentManager()	 = default;
		~InspectorComponentManager() = default;

		void				 registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector );
		IInspectorComponent* find( string_view typeName ) const;
		void				 registerDefaults();

		template <typename TComponent, typename TInspector, typename... TArgs>
		void registerComponent( TArgs&&... args )
		{
			registerType( TComponent::StaticType()->_name.c_str(),
						  make_unique<TInspector>( std::forward<TArgs>( args )... ) );
		}

	private:
		map<string, unique_ptr<IInspectorComponent>> _mapInspector;
	};
} // namespace sw::editor
