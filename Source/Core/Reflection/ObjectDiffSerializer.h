#pragma once
/**
 * @file ObjectDiffSerializer.h
 * @brief CDO-based object diff (delta) serialize/deserialize
 */

#include "Core/CoreMinimal.h"
#include "Core/Reflection/ReflectionCore.h"

namespace sw
{

	/**
	 * @class ObjectDiffSerializer
	 * @brief CDO(Class Default Object) 기본값 대비 수정된 프로퍼티 델타(Delta)만 직렬화하는 최적화 클래스
	 */
	class SW_API ObjectDiffSerializer
	{
	public:
		/** @brief CDO 객체와 변경된 객체를 비교하여 델타 바이너리 추출 */
		static bool serializeDiff( std::vector<uint8>& outDiffBuffer, const void* cdoInstance, const void* modifiedInstance, const TypeInfo& typeInfo );

		/** @brief 델타 바이너리를 타깃 인스턴스에 적용 */
		static bool deserializeDiff( void* targetInstance, const TypeInfo& typeInfo, const uint8* diffData, size_t diffSize );
	};

}
