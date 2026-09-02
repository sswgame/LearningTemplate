/**
 * @file MaterialCache.h
 * @brief 경로 키로 Material 소유권을 관리합니다 (핫스왑용 GPU 수명주기).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw

{
    class IRHIDevice;
    class Material;

    /**
     * @class MaterialCache
     * @brief 리소스 상대 경로를 키로 Material 인스턴스를 소유합니다. Scene은 포인터만 빌립니다.
     */
    class SW_API MaterialCache
    {
    public:
        /** @brief 빈 캐시. */
        MaterialCache();
        /** @brief 캐시를 해제합니다. */
        ~MaterialCache();

        /** @brief 복사를 금지합니다. */
        MaterialCache( const MaterialCache& ) = delete;
        /** @brief 대입을 금지합니다. */
        MaterialCache& operator=( const MaterialCache& ) = delete;

        /** @brief 경로의 Material을 확보하고 GPU에 올립니다. */
        Material* acquire( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 경로의 Material을 다시 로드하고 GPU 캐시를 갱신합니다. */
        void reload( string_view relativePath );
        /** @brief 경로의 Material 참조를 해제합니다. */
        void release( string_view relativePath );
        /** @brief 모든 Material의 GPU 자원을 해제합니다. */
        void shutdownAllGpu( IRHIDevice* pDevice );
        /** @brief 모든 Material을 새 디바이스에 다시 올립니다. */
        bool reinitializeAll( IRHIDevice* pDevice );
        /** @brief 캐시를 비웁니다. */
        void clear();

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw
