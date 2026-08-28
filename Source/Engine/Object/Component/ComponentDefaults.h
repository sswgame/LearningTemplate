/**
 * @file ComponentDefaults.h
 * @brief 게임 gamedata.xml `<Defaults>`를 Component PROPERTY에 주입합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
	class Component;
	struct TypeInfo;

	/**
	 * @class ComponentDefaults
	 * @brief 게임 부트스트랩이 지정한 gamedata.xml의 `<Defaults>`를 리플렉션으로 주입합니다.
	 */
	class SW_API ComponentDefaults
	{
	public:
		/** @brief 인스턴스에 XML 기본값을 리플렉션으로 주입합니다. */
		static void applyDefaults( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo = nullptr );
		/** @brief 컴포넌트 인스턴스에 XML 기본값을 리플렉션으로 주입합니다. */
		static void applyDefaults( Component* pComp, const TypeInfo& typeInfo );

		/** @brief 게임 gamedata.xml 리소스 경로를 지정합니다. 비어 있으면 주입하지 않습니다. */
		static void setDefaultsPath( string_view path );

		/** @brief 현재 게임 gamedata.xml 리소스 경로를 반환합니다. */
		static string_view getDefaultsPath();

		/** @brief 캐시된 기본값 XML 문서를 다시 로드합니다. */
		static void reload();
	};
} // namespace sw
