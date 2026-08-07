#pragma once
/**
 * @file JsonSerializer.h
 * @brief JSON serialize/deserialize using TypeInfo reflection
 */

#include "Core/CoreMinimal.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Reflection/SerializeContext.h"

namespace sw
{

	/**
	 * @class JsonSerializer
	 * @brief TypeInfo 리플렉션을 이용한 JSON 직렬화/역직렬화 클래스
	 */
	class SW_API JsonSerializer
	{
	public:
		/** @brief 한 줄 단축 JSON 직렬화 */
		static std::string serialize( const void* instance, const TypeInfo& typeInfo,
									  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 들여쓰기가 포함된 Pretty JSON 직렬화 */
		static std::string serializePretty( const void* instance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
											const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief JSON 문자열 역직렬화 */
		static bool		   deserialize( void* instance, const TypeInfo& typeInfo, std::string_view jsonStr,
										const SerializeContext& ctx = SerializeContext::getDefault() );
	};

}
