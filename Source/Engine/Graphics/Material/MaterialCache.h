/**
 * @file MaterialCache.h
 * @brief 경로 키로 Material 소유권을 관리합니다.
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
     * @note 디바이스 수명은 이 캐시의 일이 아니다. `Material` 이 `RHIRenderResource` 라서 디바이스가 죽고
     *       서는 것을 **스스로** 통보받는다 — 여기에 일괄 해제·일괄 재생성 함수를 다시 만들지 말 것.
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
        /** @brief 캐시를 비웁니다. */
        void clear();

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw
