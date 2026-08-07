#pragma once
/**
 * @file BinarySerializer.h
 * @brief Compact binary serialize/deserialize using TypeInfo
 */

#include "Core/CoreMinimal.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Reflection/SerializeContext.h"

namespace sw
{

	/**
	 * @class BinarySerializer
	 * @brief 타입 정보를 참조하여 객체를 콤팩트 바이너리 버퍼로 직렬화/역직렬화하는 클래스
	 */
	class SW_API BinarySerializer
	{
	public:
		/** @brief 기본 바이너리 직렬화 */
		static void serialize( const void* instance, const TypeInfo& typeInfo, std::vector<uint8>& outBuffer,
							   const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 기본 바이너리 역직렬화 */
		static bool deserialize( void* instance, const TypeInfo& typeInfo, const uint8* data, size_t dataSize,
								 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 버전 버퍼 헤더를 포함하는 바이너리 직렬화 */
		static void serializeVersioned( uint32 version, const void* instance, const TypeInfo& typeInfo, std::vector<uint8>& outBuffer,
										const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 버전 정보를 검증하는 바이너리 역직렬화 */
		static bool deserializeVersioned( uint32& outVersion, void* instance, const TypeInfo& typeInfo, const uint8* data, size_t dataSize,
										  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief POD memcpy 또는 binary serialize/deserialize로 객체 복제 */
		static bool cloneObject( void* dstData, const void* srcData, const TypeInfo& typeInfo );
	};

} // namespace sw
