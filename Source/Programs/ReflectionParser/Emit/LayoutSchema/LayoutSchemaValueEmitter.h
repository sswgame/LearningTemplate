/**
 * @file LayoutSchemaValueEmitter.h
 * @brief 필드 값 하나의 서술자 칸(`::sw::LayoutFieldDesc`)과 정규 문자열을 같은 순서로 냅니다.
 * @details 값의 종류(`LayoutValueKind`)마다 갈립니다. 스키마의 표 · 등록 줄은 `LayoutSchemaGenerator` 가 짭니다. 본문 코드는 내지 않는다 — 해석은 런타임 `LayoutSerializer` 하나다.
 */
#pragma once
#include "Core/String/StringBuilder.h"

#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/Emit/LayoutSchema/LayoutSchemaEmitState.h"

namespace sw
{
    /** @brief 정규 문자열을 짓는 버퍼입니다. 필드마다 임시 글을 만들지 않고 한 버퍼에 이어 붙인 뒤 끝에 한 번만 `string` 으로 옮깁니다. */
    using LayoutCanonicalBuilder = StringBuilder<constant::buffer::kMax1024>;

    /** @brief 필드 값 하나의 서술자 칸과 정규 문자열을 같은 순서로 냅니다. */
    struct LayoutSchemaValueEmitter
    {
        /** @brief 타입의 프로퍼티를 선언 순서로 @p outListNode 에 냅니다. 앞선 필드만 범위 이름으로 볼 수 있습니다. 펼친 중첩 타입이면 범위 깊이가 하나 늡니다. */
        static void emitProperties( LayoutSchemaEmitState& state, const ParsedTypeInfo& type, LayoutCanonicalBuilder& outCanonical, vector<LayoutSchemaNode>& outListNode );
    };
} // namespace sw
