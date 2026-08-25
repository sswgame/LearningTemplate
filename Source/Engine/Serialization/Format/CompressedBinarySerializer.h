#pragma once
#include "Core/Compression/CompressionStream.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
	struct TypeInfo;

	/**
	 * @class CompressedBinarySerializer
	 * @brief 압축 바이너리 직렬화/역직렬화 헬퍼
	 * @details BinarySerializer의 콤팩트 직렬화 결과물을 CompressionStream을 통해 자동 압축/역압축합니다.
	 */
	class SW_API CompressedBinarySerializer
	{
	public:
		/**
		 * @brief 객체를 바이너리로 직렬화한 뒤 지정된 코덱으로 압축합니다.
		 */
		static bool serializeCompressed( const void*			 pInstance,
										 const TypeInfo&		 typeInfo,
										 vector<uint8>&			 listOutBuffer,
										 CompressionCodecType	 codecType = CompressionCodecType::RLE,
										 const SerializeContext& ctx	   = SerializeContext::getDefault() );

		/**
		 * @brief 압축된 바이너리 스트림을 역압축하여 객체로 역직렬화합니다.
		 */
		static bool deserializeCompressed( void*				   pInstance,
										   const TypeInfo&		   typeInfo,
										   const uint8*			   pData,
										   size_t				   dataSize,
										   const SerializeContext& ctx = SerializeContext::getDefault() );

		/**
		 * @brief 버전 헤더가 포함된 압축 바이너리로 직렬화합니다.
		 */
		static bool serializeVersionedCompressed( uint32				  version,
												  const void*			  pInstance,
												  const TypeInfo&		  typeInfo,
												  vector<uint8>&		  listOutBuffer,
												  CompressionCodecType	  codecType = CompressionCodecType::RLE,
												  const SerializeContext& ctx		= SerializeContext::getDefault() );

		/**
		 * @brief 압축된 버전 바이너리 스트림을 역압축 및 마이그레이션하여 역직렬화합니다.
		 */
		static bool deserializeVersionedCompressed( uint32&					outVersion,
													void*					pInstance,
													const TypeInfo&			typeInfo,
													const uint8*			pData,
													size_t					dataSize,
													uint32					currentVersion	= 0,
													SchemaMigrateFn			migrate			= nullptr,
													const TypeInfo*			pLegacyTypeInfo = nullptr,
													const SerializeContext& ctx				= SerializeContext::getDefault() );
	};
} // namespace sw
