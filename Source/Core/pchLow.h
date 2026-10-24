/**
 * @file pchLow.h
 * @brief Core 낮은 티어(Log 보다 아래 폴더)용 프리컴파일 헤더입니다 — `CoreMinimal.h` 에서 Log · File · String 을 뺀 묶음입니다.
 * @note 낮은 티어 `.cpp` 가 pch 로 몰래 받던 위 티어 선언(`SW_LOG_*` · `StringBuilder` · `FileUtil` …)을 컴파일 오류로 만듭니다.
 *       `CoreMinimal.h` 와의 차이는 `CheckCoreLayers.py` 가 맞춰 봅니다(티어 표에서 Log 이상인 폴더의 헤더만 빠져야 합니다).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/EnumUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/array.h"
#include "Core/Container/deque.h"
#include "Core/Container/formatString.h"
#include "Core/Container/list.h"
#include "Core/Container/map.h"
#include "Core/Container/pair.h"
#include "Core/Container/set.h"
#include "Core/Container/sparse_set.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
