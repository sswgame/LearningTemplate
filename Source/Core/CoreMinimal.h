#pragma once
/**
 * @file CoreMinimal.h
 * @brief 엔진의 핵심 최소 헤더 - 기본 타입과 시스템 인터페이스 제공
 * @details
 * 이 헤더는 엔진의 가장 기본적인 기능들에 대한 접근을 제공합니다.
 * - 기본 타입 시스템 (Types.h)
 * - 플랫폼 공통 헤더 (CommonHeaders.h)
 * - 공통 매크로 (CommonMacros.h)
 * - 공통 정의 (CommonDefines.h)
 * - 플랫폼 특정 헤더 (PlatformHeaders.h)
 * - 코어 서비스 접근자 (CoreServices.h)
 * - 로깅 시스템 (Logger.h)
 * - 문자열 유틸리티 (hashed_string.h, StringUtil.h)
 * - 델리게이트 시스템 (Delegate.h)
 *
 * @note 모든 엔진 모듈은 이 헤더를 통해 기본 기능에 접근합니다.
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/CommonDefines.h"
#include "Core/Common/PlatformHeaders.h"

#include "Core/Common/CoreServices.h"

#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/String/hashed_string.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Utility/Delegate/Delegate.h"
