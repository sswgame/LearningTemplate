/**
 * @file BinarySerializer.h
 * @brief TypeInfo 기반 콤팩트 바이너리 직렬화/역직렬화 (비압축 및 압축 지원)
 */
#pragma once
#include "Core/Compression/ICompressionCodec.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
	class Archive;
	struct TypeInfo;

	/**
	 * @class BinarySerializer
	 * @brief TypeInfo로 객체를 콤팩트 바이너리 버퍼에 쓰고 읽습니다 (압축 직렬화 통합 지원)
	 */
	class SW_API BinarySerializer
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 기본 비압축 — 실패 시 deserialize는 호출 전 상태로 롤백
		// ------------------------------------------------------------------------------
		/** @brief 객체를 콤팩트 바이너리로 직렬화합니다. */
		static void serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
							   const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief Archive로 객체를 콤팩트 바이너리로 직렬화합니다. */
		static void serialize( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
							   const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 바이너리에서 객체를 역직렬화합니다. 실패 시 instance를 호출 전 상태로 되돌립니다. */
		static bool deserialize( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
								 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief Archive에서 객체를 역직렬화합니다. */
		static bool deserialize( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
								 const SerializeContext& ctx = SerializeContext::getDefault() );

		// ------------------------------------------------------------------------------
		// 2) Soft — 타입 불일치·미지 필드는 orphan으로 모으고 계속
		// ------------------------------------------------------------------------------
		/**
		 * @brief Soft 역직렬화 — 타입 불일치·미지 필드는 orphan으로 모으고 계속 진행.
		 * @details 실패해도 payload 크기만큼 스킵. tryCoerceBinaryPayload로 int32↔string 등 자동 시도.
		 */
		static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
									 vector<SchemaOrphanValue>* pOutOrphans = nullptr,
									 const SerializeContext&	ctx			= SerializeContext::getDefault() );

		// ------------------------------------------------------------------------------
		// 3) 버전 — 헤더 + migrate, clone
		// ------------------------------------------------------------------------------
		/** @brief 버전 버퍼 헤더를 포함한 바이너리 직렬화. */
		static void serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& outListBuffer,
										const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief Archive로 버전 바이너리 직렬화를 수행합니다. */
		static void serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
										const SerializeContext& ctx = SerializeContext::getDefault() );

		/**
		 * @brief 버전 헤더 + soft deserialize, 필요 시 migrate.
		 * @param pLegacyTypeInfo 제공 시 해당 스키마로 스테이징 로드 후 ctx.legacyInstance로 전달.
		 * @return migrate 없이 버전 불일치·orphan이 있으면 false.
		 */
		static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
										  uint32				  currentVersion  = 0,
										  SchemaMigrateFn		  migrate		  = nullptr,
										  const TypeInfo*		  pLegacyTypeInfo = nullptr,
										  const SerializeContext& ctx			  = SerializeContext::getDefault() );

		/** @brief Archive에서 버전 바이너리를 역직렬화합니다. */
		static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
										  uint32				  currentVersion  = 0,
										  SchemaMigrateFn		  migrate		  = nullptr,
										  const TypeInfo*		  pLegacyTypeInfo = nullptr,
										  const SerializeContext& ctx			  = SerializeContext::getDefault() );

		/** @brief POD memcpy 또는 binary serialize/deserialize로 객체를 복제합니다. */
		static bool cloneObject( void* pDstData, const void* pSrcData, const TypeInfo& typeInfo );

		// ------------------------------------------------------------------------------
		// 4) 압축 바이너리 직렬화 (CompressionStream 통합)
		// ------------------------------------------------------------------------------
		/** @brief 객체를 바이너리로 직렬화한 뒤 지정된 코덱으로 압축합니다. */
		static bool serializeCompressed( const void*			 pInstance,
										 const TypeInfo&		 typeInfo,
										 vector<uint8>&			 outListBuffer,
										 CompressionCodecType	 codecType = CompressionCodecType::RLE,
										 const SerializeContext& ctx	   = SerializeContext::getDefault() );

		/** @brief Archive로 압축 바이너리 직렬화를 수행합니다. */
		static bool serializeCompressed( const void*			 pInstance,
										 const TypeInfo&		 typeInfo,
										 Archive&				 outArchive,
										 CompressionCodecType	 codecType = CompressionCodecType::RLE,
										 const SerializeContext& ctx	   = SerializeContext::getDefault() );

		/** @brief 압축된 바이너리 스트림을 역압축하여 객체로 역직렬화합니다. */
		static bool deserializeCompressed( void*				   pInstance,
										   const TypeInfo&		   typeInfo,
										   const uint8*			   pData,
										   size_t				   dataSize,
										   const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief Archive에서 압축 바이너리를 역직렬화합니다. */
		static bool deserializeCompressed( void*				   pInstance,
										   const TypeInfo&		   typeInfo,
										   Archive&				   inArchive,
										   const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 버전 헤더가 포함된 압축 바이너리로 직렬화합니다. */
		static bool serializeVersionedCompressed( uint32				  version,
												  const void*			  pInstance,
												  const TypeInfo&		  typeInfo,
												  vector<uint8>&		  outListBuffer,
												  CompressionCodecType	  codecType = CompressionCodecType::RLE,
												  const SerializeContext& ctx		= SerializeContext::getDefault() );

		/** @brief 압축된 버전 바이너리 스트림을 역압축 및 마이그레이션하여 역직렬화합니다. */
		static bool deserializeVersionedCompressed( uint32&					outVersion,
													void*					pInstance,
													const TypeInfo&			typeInfo,
													const uint8*			pData,
													size_t					dataSize,
													uint32					currentVersion	= 0,
													SchemaMigrateFn			migrate			= nullptr,
													const TypeInfo*			pLegacyTypeInfo = nullptr,
													const SerializeContext& ctx				= SerializeContext::getDefault() );

		// ------------------------------------------------------------------------------
		// 5) 적응형 컴팩트 바이너리 직렬화 (Presence Bitmask & Sparse VarUInt Index)
		// ------------------------------------------------------------------------------
		/**
		 * @brief 적응형 컴팩트 바이너리 직렬화 (Dense: Bitmask, Sparse: VarUInt Index)
		 * @details 12바이트 태그 헤더 대신 밀집도(Dense vs Sparse)에 따라 비트마스크(8~32B) 또는 1바이트 인덱스를 기록하여 헤더 용량을 최대 98% 절감합니다.
		 */
		static void serializeCompact( const void*			  pInstance,
									  const TypeInfo&		  typeInfo,
									  vector<uint8>&		  outBuffer,
									  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief Archive로 적응형 컴팩트 바이너리 직렬화를 수행합니다. */
		static void serializeCompact( const void*			  pInstance,
									  const TypeInfo&		  typeInfo,
									  Archive&				  outArchive,
									  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 적응형 컴팩트 바이너리 버퍼에서 객체를 역직렬화합니다. */
		static bool deserializeCompact( void*					pInstance,
										const TypeInfo&			typeInfo,
										const uint8*			pData,
										size_t					dataSize,
										const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief Archive에서 적응형 컴팩트 바이너리를 역직렬화합니다. */
		static bool deserializeCompact( void*					pInstance,
										const TypeInfo&			typeInfo,
										Archive&				inArchive,
										const SerializeContext& ctx = SerializeContext::getDefault() );
	};

} // namespace sw
