/**
 * @file Common.h
 * @brief 엔진 전체에서 공통으로 쓰는 핵심 헤더를 한 번에 include 합니다.
 * @details Math, String, File, Time 등 엔진 곳곳에서 자주 쓰는 유틸리티를 묶어 둡니다. 티어 0 우산이라 위 티어 헤더는 넣지 않습니다.
 *          리소스 헬퍼(`Content/AssetIO.h` · `Content/AssetPaths.h`)는 쓰는 파일이 직접 include 합니다.
 */
#pragma once
#include "Core/Container/formatString.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/Math.h"
#include "Core/String/fixed_string.h"
#include "Core/String/string_splitter.h"
#include "Core/Time/GameTimer.h"

#include "Engine/EngineMinimal.h"
