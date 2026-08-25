/**
 * @file ObjectDiffSerializer.h
 * @brief CDO 대비 객체 델타(diff) 직렬화/역직렬화
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
	struct TypeInfo;

	/**
	 * @class ObjectDiffSerializer
	 * @brief CDO 기본값과 다른 프로퍼티만 델타로 씁니다
	 */
	class SW_API ObjectDiffSerializer
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) Diff — CDO 대비 변경분만, 와이어는 nameHash+size+payload
		// ------------------------------------------------------------------------------
		/** @brief CDO와 변경된 객체를 비교해 델타 바이너리를 추출합니다. */
		static bool serializeDiff( vector<uint8>& listOutDiffBuffer, const void* pCdoInstance, const void* pModifiedInstance,
								   const TypeInfo& typeInfo );

		/**
		 * @brief serializeDiff 델타 바이너리를 적용합니다.
		 * @details 와이어 레이아웃: nameHash + size + payload (typeHash 없음; 현재 PropertyInfo로 타입 해석).
		 *          BinarySerializer 프로퍼티 레코드(nameHash + typeHash + size + payload)와 다릅니다.
		 *          알 수 없는 프로퍼티 해시는 적용 실패(false).
		 */
		static bool deserializeDiff( void* pTargetInstance, const TypeInfo& typeInfo, const uint8* pDiffData, size_t diffSize );
	};

} // namespace sw
