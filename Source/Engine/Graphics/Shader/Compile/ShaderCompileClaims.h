/**
 * @file ShaderCompileClaims.h
 * @brief 지금 컴파일하는 글의 표입니다 — 글이 같은 요청 여럿이 동시에 와도 한 스레드만 컴파일합니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <condition_variable>

namespace sw
{
    /**
     * @class ShaderCompileClaims
     * @brief 글 키마다 하나인 잠금입니다. 실행 중 컴파일(공급자 `LiveShaderBytecodeProvider`)이 하나 듭니다.
     * @details 같은 요청은 `ShaderCache` 가 합치지만 define 만 다르고 글이 같은 요청들은 저마다 컴파일했다(묶음 컴파일은 `ShaderCompiler::compileBatch` 가 합친다).
     *          기다린 쪽은 DDC 에서 그 글의 바이트코드를 찾는다. 컴파일은 작업 풀을 기다리지 않으므로 기다리는 워커가 컴파일을 막지 않는다. 언리얼 `FShaderJobCache` 의 실행 중 작업 합치기 자리다.
     */
    class SW_API ShaderCompileClaims
    {
    public:
        /** @brief 글 하나를 맡은 범위입니다. 사라질 때 놓습니다. */
        class Scope
        {
        public:
            Scope( ShaderCompileClaims& claims, uint64 textKey );
            ~Scope();
            Scope( const Scope& )            = delete;
            Scope& operator=( const Scope& ) = delete;

        private:
            ShaderCompileClaims& _claims;
            uint64               _textKey;
        };

        ShaderCompileClaims();
        ShaderCompileClaims( const ShaderCompileClaims& )            = delete;
        ShaderCompileClaims& operator=( const ShaderCompileClaims& ) = delete;

    private:
        /** @brief 글 @p textKey 를 맡습니다. 다른 스레드가 맡고 있으면 놓을 때까지 기다립니다. */
        void acquire( uint64 textKey );
        void release( uint64 textKey );

    private:
        mutex                                _mutex;
        std::condition_variable_any          _cvReleased;
        unordered_set<uint64> _uniqueTextKey SW_GUARDED_BY( _mutex ); ///< 지금 컴파일하는 글
    };
} // namespace sw
