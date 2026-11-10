/**
 * @file PrefabPathStack.h
 * @brief 지금 펼치는 중인 프리팹 경로의 스택(순환 검사)입니다. 에셋의 변형 읽기와 캐시의 로드 · 스폰 · 펼치기가 함께 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    /**
     * @struct PrefabPathStack
     * @brief 프리팹 경로 스택의 형식과 그 구간 도구입니다.
     */
    struct PrefabPathStack
    {
        /**
         * @brief 힙 없이 담는 깊이입니다.
         * @details 스레드 지역 스택이라 힙에 잡으면 처음 스폰한 자리의 태그(Scene)로 스레드가 끝날 때까지 남는다 — 종료 보고는
         *          스레드보다 먼저 온다. 더 깊게 중첩하면 그때만 힙을 쓴다.
         */
        static constexpr size_t kInlineDepth = 8;

        using List = small_vector<hashed_string, kInlineDepth>;

        /** @brief 스택에 넣은 경로를 구간이 끝날 때 뺍니다. */
        struct Scope
        {
            List& _stack;

            Scope( List& stack, const hashed_string& key )
                : _stack{ stack }
            {
                _stack.push_back( key );
            }
            ~Scope()
            {
                _stack.pop_back();
            }
            Scope( const Scope& )            = delete;
            Scope& operator=( const Scope& ) = delete;
        };

        /** @brief 스택에 넣는 키입니다(캐시 키 `PrefabCache::makeCacheKey` 를 인턴한 것). */
        static hashed_string makeKey( const string& cacheKey )
        {
            return hashed_string( cacheKey.data(), static_cast<uint32>( cacheKey.size() ) );
        }
    };
} // namespace sw
