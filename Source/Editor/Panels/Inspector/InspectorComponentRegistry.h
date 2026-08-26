/**
 * @file InspectorComponentRegistry.h
 * @brief 컴포넌트 타입명 → IInspectorComponent
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	class IInspectorComponent;

	/** @brief 컴포넌트 타입별 인스펙터 UI 레지스트리 */
	class InspectorComponentRegistry
	{
	public:
		InspectorComponentRegistry()  = default;
		~InspectorComponentRegistry() = default;

		static void					 registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector );
		static IInspectorComponent* find( string_view typeName );
		static void					 registerDefaults();

		template <typename TComponent, typename TInspector, typename... TArgs>
		static void registerComponent( TArgs&&... args )
		{
			registerType( TComponent::StaticType()->_name.c_str(),
						  make_unique<TInspector>( std::forward<TArgs>( args )... ) );
		}

		void					registerTypeImpl( string_view typeName, unique_ptr<IInspectorComponent> pInspector );
		IInspectorComponent*	findImpl( string_view typeName ) const;
		void					registerDefaultsImpl();

	private:
		map<string, unique_ptr<IInspectorComponent>> _mapInspectors;
	};
} // namespace sw
