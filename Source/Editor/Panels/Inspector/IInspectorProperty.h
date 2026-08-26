/**
 * @file IInspectorProperty.h
 * @brief 리플렉션 프로퍼티 타입별 인스펙터 편집 UI
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	struct PropertyInfo;

	/** @brief 프로퍼티 타입 하나의 인스펙터 위젯 */
	class IInspectorProperty
	{
	public:
		virtual ~IInspectorProperty() = default;

		/**
		 * @brief 프로퍼티 UI를 그립니다.
		 * @return 값이 바뀌었으면 true
		 */
		virtual bool draw( void* pInstance, const PropertyInfo& prop ) = 0;
	};
} // namespace sw
