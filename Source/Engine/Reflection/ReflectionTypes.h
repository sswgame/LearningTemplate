/**
 * @file ReflectionTypes.h
 * @brief 리플렉션용 프로퍼티 / enum / 함수 / 타입 정보
 * @details `GameObject.h` · `Component.h` 를 거쳐 TU 약 1,200 개가 읽는 헤더다. 속성 키를 더할 때마다 바뀌는 메타데이터 구조
 *          (`TypeMetadata` · `PropertyMetadata` · `FunctionMetadata`)는 `ReflectionMetadata.h` 에 있고, 여기서는 `ReflectionMetadataBox` 로만 든다.
 *          속성 키 · 메타데이터 칸을 더할 때는 `ReflectionMetadata.h` 만 고친다 — 이 헤더에 메타데이터 칸을 읽는 인라인 본문을 두지 않는다.
 */
#pragma once
#include "Engine/Reflection/TypeInfo.h"
