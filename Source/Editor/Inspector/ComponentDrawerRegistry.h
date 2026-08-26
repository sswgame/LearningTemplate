#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	class IComponentDrawer;

	/**
	 * @class ComponentDrawerRegistry
	 * @brief 컴포넌트 타입별 IComponentDrawer를 관리하고 제공하는 정적 레지스트리
	 */
	class ComponentDrawerRegistry
	{
	public:
		ComponentDrawerRegistry()  = default;
		~ComponentDrawerRegistry() = default;

		// ------------------------------------------------------------------------------
		// 정적(Static) 공개 API
		// ------------------------------------------------------------------------------
		static void				 registerDrawer( string_view typeName, unique_ptr<IComponentDrawer> drawer );
		static IComponentDrawer* getDrawer( string_view typeName );
		static void				 registerDefaultDrawers();

		template <typename TComponent, typename TDrawer, typename... TArgs>
		static void registerComponentDrawer( TArgs&&... args )
		{
			registerDrawer( TComponent::StaticType()->_name.c_str(),
							make_unique<TDrawer>( std::forward<TArgs>( args )... ) );
		}

		// ------------------------------------------------------------------------------
		// 인스턴스 메서드 (EditorContext 소유)
		// ------------------------------------------------------------------------------
		void			  registerDrawerImpl( string_view typeName, unique_ptr<IComponentDrawer> drawer );
		IComponentDrawer* getDrawerImpl( string_view typeName ) const;
		void			  registerDefaultDrawersImpl();

	private:
		map<string, unique_ptr<IComponentDrawer>> _mapDrawers;
	};
} // namespace sw
