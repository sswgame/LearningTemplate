/**
 * @file ShaderBindingLayoutCache.h
 * @brief (셰이더 경로 + define + 백엔드) → ShaderBindingLayout 캐시.
 * @details
 *  PSO 생성 시 이 캐시로 레이아웃을 얻는다. 내부적으로 활성 백엔드 타깃 포맷으로
 *  셰이더를 컴파일(ShaderCache 디스크 캐시 재사용)하고 ShaderReflection 으로
 *  리플렉션한 뒤 ShaderBindingLayout::build 한다. 핫리로드 시 파일 경로로 무효화한다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Shader/ShaderBindingLayout.h"

namespace sw
{
    enum class RHIBackend : uint32;

    struct RHIPipelineStateDesc;

    /**
     * @class ShaderBindingLayoutCache
     * @brief PSO 변형별 ShaderBindingLayout 캐시. 스레드 안전.
     */
    class SW_API ShaderBindingLayoutCache
    {
    public:
        /** @brief 빈 캐시. */
        ShaderBindingLayoutCache() = default;

        /** @brief 복사를 금지합니다. */
        ShaderBindingLayoutCache( const ShaderBindingLayoutCache& )            = delete;
        ShaderBindingLayoutCache& operator=( const ShaderBindingLayoutCache& ) = delete;

        /**
         * @brief PSO 서술로 레이아웃을 얻습니다. 캐시에 있으면 즉시 반환합니다.
         * @param backend 리플렉션에 쓸 셰이더 타깃 포맷을 결정하는 실제 활성 백엔드.
         *        전역 상태를 쓰지 않는다 — 한 프로세스에 여러 IRHIDevice 가 공존하는
         *        경로(멀티 백엔드 테스트 등)에서 전역값이 실제 디바이스와 어긋나면 엉뚱한 셰이더
         *        변형을 리플렉션해 바인딩 슬롯이 통째로 빠지는 문제가 생긴다.
         * @details 그래픽스 PSO 는 VS+PS(존재 시 GS 등), 컴퓨트 PSO 는 CS 를 리플렉션합니다.
         *          컴파일/리플렉션 실패 시 빈 레이아웃을 캐시하고 반환합니다.
         * @return 캐시가 소유하는 레이아웃 참조 (캐시 수명 동안 유효).
         */
        const ShaderBindingLayout& getOrBuild( const RHIPipelineStateDesc& desc, RHIBackend backend );

        /** @brief 주어진 셰이더 파일 경로에 의존하는 캐시 항목을 모두 제거합니다 (핫리로드). */
        void invalidateByShaderPath( string_view shaderRelativePath );

        /** @brief 전체 캐시를 비웁니다. */
        void clear();

        /** @brief 캐시 항목 수. */
        uint32 getEntryCount() const;

    private:
        struct CacheEntry
        {
            ShaderBindingLayout _layout;
            vector<string>      _listSourcePath; ///< 무효화 매칭용 (VS/PS/CS 경로)
        };

        hashed_string makeCacheKey( const RHIPipelineStateDesc& desc, RHIBackend backend ) const;

        // unique_ptr 로 저장해 unordered_map 리해시가 일어나도 반환한 참조/포인터가 안정적이게 한다
        // (병렬 커맨드 기록에서 layoutForPso 포인터가 재배치로 무효화되면 데이터 레이스).
        mutable mutex                                        _mutex;
        unordered_map<hashed_string, unique_ptr<CacheEntry>> _mapEntry;
        ShaderBindingLayout                                  _emptyLayout;
    };
} // namespace sw
