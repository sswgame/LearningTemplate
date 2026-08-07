#pragma once

/**
 * @file Common.h
 * @brief 엔진 전체에서 공통으로 사용되는 핵심 모듈 헤더들을 단일 경로로 제공합니다.
 * @details Math, String, File, Resource, Time 등 엔진 전역에서 빈번하게 참조되는 유틸리티들을 묶어서 편의성을 높입니다.
 */

#include "Core/CoreMinimal.h"

// ============================================================================
// [공통 유틸리티 포함 영역]
// ============================================================================
#include "Core/Utility/Math/Math.h"
#include "Core/Utility/String/formatString.h"
#include "Core/Utility/String/string_splitter.h"
#include "Core/Utility/String/fixed_string.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/Time/EngineTimer.h"
