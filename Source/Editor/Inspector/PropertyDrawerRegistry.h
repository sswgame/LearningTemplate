#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	class IPropertyDrawer;

	/** @brief 타입별 IPropertyDrawer를 관리하고 제공하는 정적 레지스트리 */
	class PropertyDrawerRegistry
	{
	public:
		PropertyDrawerRegistry()  = default;
		~PropertyDrawerRegistry() = default;

		// ------------------------------------------------------------------------------
		// 정적(Static) 공개 API
		// ------------------------------------------------------------------------------
		static void				registerDrawer( string_view typeName, unique_ptr<IPropertyDrawer> drawer );
		static IPropertyDrawer* getDrawer( string_view typeName );
		static void				registerDefaultDrawers();

		// ------------------------------------------------------------------------------
		// 인스턴스 메서드 (EditorContext 소유)
		// ------------------------------------------------------------------------------
		void			 registerDrawerImpl( string_view typeName, unique_ptr<IPropertyDrawer> drawer );
		IPropertyDrawer* getDrawerImpl( string_view typeName ) const;
		void			 registerDefaultDrawersImpl();

	private:
		map<string, unique_ptr<IPropertyDrawer>> _mapDrawers;
	};
} // namespace sw
