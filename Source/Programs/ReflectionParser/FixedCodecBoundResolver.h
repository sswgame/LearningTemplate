/**
 * @file FixedCodecBoundResolver.h
 * @brief `Min` · `Max` 의 항을 숫자나 앞선 필드의 식으로 풀고, 코덱 호출에 넣을 범위 식을 만듭니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/FixedCodecEmitState.h"

namespace sw
{
    /** @brief `Min` · `Max` 의 항을 숫자나 앞선 필드의 식으로 풀고, 코덱 호출에 넣을 범위 식을 만듭니다. */
    struct FixedCodecBoundResolver
    {
        /**
         * @brief `Min` 또는 `Max` 글을 항마다 풉니다.
         * @return 생성 오류이거나 이름을 찾지 못했으면 false 입니다. 이름을 못 찾은 것은 `state._unresolvedName` 에 남습니다.
         */
        [[nodiscard]] static bool resolveBounds( FixedCodecEmitState& state, const ParsedPropertyInfo& prop, const string& text, bool bInteger, vector<FixedCodecBound>& outListBound );
        /** @brief 원소 @p elementIndex 의 범위입니다. 항이 하나면 모든 원소가 같습니다. */
        static const FixedCodecBound& pickBound( const vector<FixedCodecBound>& listBound, size_t elementIndex );
        static string                 makeIntegerBound( const FixedCodecBound& bound, bool bLower );
        static string                 makeFloatBound( const FixedCodecBound& bound, bool bLower, const utf8* pCastType );
    };
} // namespace sw
