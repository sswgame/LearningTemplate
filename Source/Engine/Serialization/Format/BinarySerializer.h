/**
 * @file BinarySerializer.h
 * @brief TypeInfo 기반 콤팩트 바이너리 직렬화/역직렬화
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
	struct TypeInfo;

	/**
	 * @class BinarySerializer
	 * @brief TypeInfo로 객체를 콤팩트 바이너리 버퍼에 쓰고 읽습니다
	 */
	class SW_API BinarySerializer
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 기본 — 실패 시 deserialize는 호출 전 상태로 롤백
		// ------------------------------------------------------------------------------
		/** @brief 객체를 콤팩트 바이너리로 직렬화합니다. */
		static void serialize( const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& listOutBuffer,
							   const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 바이너리에서 객체를 역직렬화합니다. 실패 시 instance를 호출 전 상태로 되돌립니다. */
		static bool deserialize( void* pInstance, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize,
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
		static void serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo, vector<uint8>& listOutBuffer,
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

		/** @brief POD memcpy 또는 binary serialize/deserialize로 객체를 복제합니다. */
		static bool cloneObject( void* pDstData, const void* pSrcData, const TypeInfo& typeInfo );
	};

} // namespace sw
