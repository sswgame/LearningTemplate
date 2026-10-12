/**
 * @file FractureAssetCache.h
 * @brief `.fracture` 를 경로별로 나눠 주는 테이블과, 그 테이블을 에셋 캐시 레지스트리에 노출하는 함수입니다. 테이블은 약한 참조를 쓰므로 마지막 사용자가 놓으면 사라집니다.
 * @details `SkeletonCache` 와 `AnimationClipCache` 처럼 `SharedAssetTable` 위에 구현한다. 다만 핫 리로드는 제자리에서 바꾸지 않고 테이블에서 **분리한다**(`detachShared`).
 *          쓰던 쪽의 조각 바디와 메시가 이전 형상을 가리킨 채 내용만 바뀌는 일이 없게 하려는 것이다.
 *          쓰는 쪽은 `getReloadGeneration` 이 바뀌면 다시 받아 다시 만든다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    struct FractureAsset;

    /** @class FractureAssetCache @brief 파일 머리말을 참고하세요. */
    class SW_API FractureAssetCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 파쇄 에셋을 공유해 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 를 반환합니다. 워커에서 호출해도 됩니다(잠급니다). */
        static shared_ptr<const FractureAsset> acquire( string_view path );
        /** @brief 쓰는 중이면 다시 읽어 테이블의 항목을 바꾸고 세대를 올립니다. 읽지 못하면 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] static bool reloadShared( string_view path );
        /** @brief 다시 읽을 때마다 오르는 세대를 반환합니다. 쓰는 쪽은 값이 바뀌면 다시 받아야 합니다. */
        static uint64 getReloadGeneration();

        const utf8* getAssetKindName() const override { return "Fracture"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw
