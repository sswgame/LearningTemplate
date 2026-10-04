/**
 * @file FractureAssetCache.h
 * @brief `.fracture` 를 경로로 나눠 주는 표(약한 참조 — 마지막 사용자가 놓으면 사라짐)와, 그 표를 에셋 캐시 등록부에 보이는 창구입니다.
 * @details `SkeletonCache` · `AnimClipCache` 와 같은 모양입니다. 핫 리로드는 표의 항목을 **새 객체로 바꿉니다**(쓰던 쪽의 조각 바디 · 메시가 옛 형상을
 *          가리키는 채로 내용만 바뀌지 않게) — 쓰는 쪽은 `getReloadGeneration` 이 바뀌면 다시 받아 다시 짓습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/IAssetCache.h"

namespace sw
{
    struct FractureAsset;

    /** @class FractureAssetCache @brief 파일 머리말 참고. */
    class SW_API FractureAssetCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 파쇄 에셋을 나눠 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다. 워커에서 불러도 됩니다(잠급니다). */
        static shared_ptr<const FractureAsset> acquire( string_view path );
        /** @brief 쓰는 중이면 다시 읽어 표의 항목을 바꾸고 세대를 올립니다. 읽지 못하면 그대로이고 false 입니다. */
        [[nodiscard]] static bool reloadShared( string_view path );
        /** @brief 다시 읽을 때마다 오르는 세대입니다(쓰는 쪽이 값이 바뀌면 다시 받는다). */
        static uint64 getReloadGeneration();

        const utf8* getAssetKindName() const override { return "Fracture"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw
