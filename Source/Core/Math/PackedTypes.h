/**
 * @file PackedTypes.h
 * @brief 저장 전용 압축 타입 전부(`half` · `unorm`/`snorm` · `OctahedralNormal` · `RGB10A2`)와 성분 규칙(`PackedValueTraits`)의 완전한 정의를 한 번에 가져옵니다.
 * @details `ReflectBuiltins.xxx` 의 표를 펼쳐 값 크기나 값 자체가 필요한 번역 단위(직렬화 · 인자 변환 · 인스펙터 · 생성 코드)가 씁니다.
 *          이름만 필요한 헤더는 `PackedTypesFwd.h` 를 씁니다. 새 압축 타입 헤더를 만들면 여기에 더합니다(표에 줄을 더하는 쪽이 따로 include 를 고르지 않게).
 */
#pragma once
#include "Core/Math/Half.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/OctahedralNormal.h"
#include "Core/Math/PackedValueTraits.h"
#include "Core/Math/RGB10A2.h"
