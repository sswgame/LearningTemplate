#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	struct PropertyInfo;

	class IPropertyDrawer
	{
	public:
		virtual ~IPropertyDrawer() = default;

		/**
		 * @brief 프로퍼티의 UI 렌더링 로직을 수행합니다.
		 * @param pInstance 프로퍼티가 속한 인스턴스의 포인터
		 * @param prop 대상 PropertyInfo
		 * @return 값이 변경되었는지 여부
		 */
		virtual bool draw( void* pInstance, const PropertyInfo& prop ) = 0;
	};
} // namespace sw
