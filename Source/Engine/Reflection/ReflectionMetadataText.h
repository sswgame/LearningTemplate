/**
 * @file ReflectionMetadataText.h
 * @brief 생성 코드(.gen.cpp · .gen.meta.cpp)가 메타데이터의 글 칸(`string` · `hashed_string` · 커스텀 메타 맵)을 채울 때 부르는 비인라인 도우미입니다.
 * @details 생성 코드가 `m._defaultValue = "..."` 처럼 적으면 문자열 대입(할당 · 복사 · 길이별 `hashed_string` 템플릿)이 TU 마다 인라인으로 펼쳐져
 *          Release 최적화 시간을 키운다. 생성 코드는 리터럴 포인터만 넘기고 대입은 Engine 의 한 곳(`ReflectionMetadata.cpp`)이 한다
 *          (언리얼 UHT 가 메타데이터를 `FMetaDataPairParam` 표 — 리터럴 포인터 쌍 — 로 넘기고 엔진이 채우는 것과 같은 자리).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    struct SW_API ReflectionMetadataText
    {
        /** @brief @p outText 를 @p pText 로 바꿉니다. */
        static void assign( string& outText, const utf8* pText );
        /** @brief @p outName 을 @p pText 의 이름으로 바꿉니다. */
        static void assign( hashed_string& outName, const utf8* pText );
        /** @brief 커스텀 메타 한 쌍을 더합니다. 같은 키가 이미 있으면 먼저 적은 값이 남습니다(초기화 목록 `= { … }` 과 같은 규칙). */
        static void addCustomMeta( unordered_map<hashed_string, string>& outMap, const utf8* pKey, const utf8* pValue );
    };
} // namespace sw
