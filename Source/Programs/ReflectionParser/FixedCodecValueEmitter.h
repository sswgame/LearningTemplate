/**
 * @file FixedCodecValueEmitter.h
 * @brief 필드 값 하나의 서술자 칸(`::sw::FixedFieldDesc`)과 정규 문자열을 같은 순서로 냅니다.
 * @details 값의 종류(`FixedValueKind`)마다 갈립니다. 코덱의 표 · 등록 줄은 `FixedCodecGenerator` 가 짭니다. 본문 코드는 내지 않는다 — 해석은 런타임 `FixedLayout` 하나다.
 */
#pragma once
#include "Core/String/StringBuilder.h"

#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/FixedCodecEmitState.h"

namespace sw
{
    /** @brief 정규 문자열을 짓는 버퍼입니다. 필드마다 임시 글을 만들지 않고 한 버퍼에 이어 붙인 뒤 끝에 한 번만 `string` 으로 옮깁니다. */
    using FixedCanonicalBuilder = StringBuilder<constant::buffer::kMax1024>;

    /** @brief 필드 값 하나의 서술자 칸과 정규 문자열을 같은 순서로 냅니다. */
    struct FixedCodecValueEmitter
    {
        /** @brief 타입의 프로퍼티를 선언 순서로 @p outListNode 에 냅니다. 앞선 필드만 범위 이름으로 볼 수 있습니다. 펼친 중첩 타입이면 범위 깊이가 하나 늡니다. */
        static void emitProperties( FixedCodecEmitState& state, const ParsedTypeInfo& type, FixedCanonicalBuilder& outCanonical, vector<FixedCodecNode>& outListNode );
    };
} // namespace sw
