/**
 * @file ModuleTypeRegistry.h
 * @brief 동적 로드 모듈의 리플렉션 타입 및 컴포넌트 팩토리 등록/정리 인터페이스.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
	struct TypeRegistrar;
	struct EnumRegistrar;
	struct ComponentFactoryRegistrar;

	namespace engine
	{
		/** @brief DLL 로드 직후: 해당 모듈의 리플렉션 타입, 컴포넌트 팩토리를 등록합니다. */
		SW_API void registerModuleTypes( string_view moduleName );

		/** @brief DLL 로드 직후: 지정된 헤드 포인터들로부터 모듈의 타입들을 등록합니다. */
		SW_API void registerModuleTypes(
			string_view					   moduleName,
			TypeRegistrar*				   pTypeHead,
			EnumRegistrar*				   pEnumHead,
			sw::ComponentFactoryRegistrar* pFactoryHead );

		/** @brief DLL 언로드 직전: 해당 모듈의 리플렉션 타입, 컴포넌트 팩토리를 정리합니다. */
		SW_API void unregisterModuleTypes( string_view moduleName );
	} // namespace engine
} // namespace sw
