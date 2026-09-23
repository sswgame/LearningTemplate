/**
 * @file Common.h
 * @brief 엔진 전체에서 공통으로 쓰는 핵심 헤더를 한 번에 include 합니다.
 * @details Math, String, File, Resource, Time 등 엔진 곳곳에서 자주 쓰는 유틸리티를 묶어 둡니다.
 */
#pragma once
#include "Core/File/FileUtil.h"
#include "Core/Math/Math.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/String/string_splitter.h"
#include "Core/Time/CpuTimer.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Resource/ResourceUtil.h"
