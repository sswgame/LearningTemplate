/**
 * @file CrashHandler.h
 * @brief 미처리 예외 / 치명적 시그널을 잡아 심볼화된 콜 스택을 로그로 남깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/**
	 * @class CrashHandler
	 * @brief 프로세스 전역 크래시 핸들러입니다.
	 * @details 설치 후 접근 위반/시그널이 나면 예외 코드·주소와 폴트 스레드의 콜 스택을
	 *          로그에 남기고 나서 기존 동작(프로세스 종료)으로 넘깁니다.
	 *          로그가 없으면 크래시 원인을 전혀 알 수 없으므로 부팅 초기에 설치합니다.
	 */
	class SW_API CrashHandler
	{
	public:
		/** @brief 핸들러를 설치합니다. 두 번 호출해도 한 번만 설치됩니다. */
		static void initialize();
		/** @brief 핸들러를 제거하고 이전 핸들러를 복구합니다. */
		static void shutdown();
	};
} // namespace sw
