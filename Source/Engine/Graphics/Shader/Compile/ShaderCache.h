/**
 * @file ShaderCache.h
 * @brief 컴파일 결과 캐시 매니저
 * @note ResourceManager가 아닙니다. 셰이더 바이트코드는 RHI/컴파일러 수명이며 팩 에셋 인스턴스와 분리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    /// @brief 디스크 캐시 한 항목 (바이트코드 + 해시)
    struct ShaderCacheEntry
    {
        uint64              _lastTimestamp{ 0 };
        ShaderCompileResult _result;
    };

    /// @brief 컴파일 결과 디스크 캐시 매니저
    class SW_API ShaderCache
    {
    public:
        ShaderCache();
        ~ShaderCache();

        ShaderCache( const ShaderCache& )            = delete;
        ShaderCache& operator=( const ShaderCache& ) = delete;

        /** @brief 셰이더 캐시를 초기화합니다. */
        bool initialize();
        /** @brief 셰이더 캐시를 정리하고 종료합니다. */
        void shutdown();

        /** @brief 캐시에 있으면 반환하고, 없거나 파일이 바뀌었으면 컴파일 후 캐시합니다. */
        ShaderCompileResult getOrCompile( const ShaderCompileDesc& desc );
        /** @brief 컴파일 캐시를 비웁니다. */
        void clearCache();

        /**
         * @brief 이 컴파일 요청이 읽을 사전 베이크 바이너리의 리소스 상대 경로 (`<domain>/shaders/bin/<rhi>/<이름>`).
         * @details 파일 이름은 `ShaderBaker::computeBinaryFileName` 그대로다 — 스템·스테이지·진입점·**퍼뮤테이션 해시**.
         *          예전엔 여기서 스템과 스테이지만으로 이름을 만들어 해시가 빠졌다. 베이커는 퍼뮤테이션마다 다른 파일을
         *          구워 두는데 캐시는 늘 해시 0(define 없음) 파일을 읽었으므로, 베이크 바이너리가 있는 한 어떤
         *          define 도 GPU 에 닿지 않았다(SW_FORWARD·MATERIAL_BLEND_TRANSLUCENT·SW_VIEWMODE_UNLIT 전부).
         *          리플렉션 매니페스트(ShaderReflectionLibrary::tryGet)는 해시를 넣은 키로 찾고 있었으니 레이아웃은
         *          맞고 바이트코드만 틀린, 가장 조용한 종류의 어긋남이었다.
         */
        static string makePrebakedRelativePath( const ShaderCompileDesc& desc );
        /**
         * @brief 이 컴파일 요청의 로컬 라이브 캐시 경로 (`Saved/ShaderCache/<rhi>/<이름>`). 이름 규칙은 위와 같다.
         * @details 런타임 컴파일 결과와 LiveShaderManager 의 재컴파일 결과가 같은 자리에 쓰인다 — 여기도 해시가 빠져
         *          있으면 처음 컴파일된 퍼뮤테이션 하나가 같은 셰이더의 다른 퍼뮤테이션 전부를 덮는다.
         */
        static string makeLocalCachePath( const ShaderCompileDesc& desc );

    private:
        unordered_map<string, ShaderCacheEntry> _mapCache;
        mutex                                   _mutexCache;
    };
} // namespace sw
