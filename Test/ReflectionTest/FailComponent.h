#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
	/**
	 * @brief Component를 상속받으면서 멤버 변수를 가지지만 REFLECT_SCRIPT() 대신 REFLECT()를 사용함.
	 * @details 이 클래스는 ReflectionParser에 의해 생성된 .gen.cpp 내의 static_assert에서 
	 * 컴파일 에러를 발생시켜야 정상입니다.
	 */
	REFLECT()
	struct FailComponent : public Component
	{
		REFLECT_BODY();

		int32 _badVariable{ 0 }; 
	};
}

// force reparse
