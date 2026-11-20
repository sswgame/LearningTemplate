/**
 * @file SharedAssetLoadOperation.h
 * @brief 공유 에셋 로드 하나의 공유 상태(`SharedAssetCache<에셋>::request` 의 핸들이 가리킨다)입니다.
 * @details 캐시 템플릿(`SharedAssetCache.h`)과 나눴다 — 캐시를 선언만 하는 종류 헤더 · `acquire` 만 쓰는 TU 는 로드 핸들 정의(`AssetHandle.h`)를 끌지 않는다.
 *          핸들(`SharedAssetHandle<에셋>`)을 값으로 들거나 `request` 를 부르는 쪽이 이 헤더를 포함한다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryTag.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"

namespace sw
{
    /**
     * @class SharedAssetLoadOperation
     * @brief 공유 에셋 로드 하나의 공유 상태입니다. 로드 단계에서 파일을 읽어 표에 넣습니다(완료 단계 없음 — 디바이스를 쓰지 않습니다).
     */
    template <typename TAsset>
    class SharedAssetLoadOperation final : public AssetLoadOperation
    {
    public:
        using AssetType = const TAsset;

        SharedAssetLoadOperation( const utf8* pKindName, string_view path, MemoryTag memoryTag )
            : AssetLoadOperation{ pKindName, path, memoryTag }
            , _pAsset{}
        {
        }

        /** @brief 읽은 에셋입니다. `Ready` 일 때만 값이 있습니다(`AssetHandle::get` 이 상태를 본다). */
        const shared_ptr<const TAsset>& getAsset() const { return _pAsset; }

    protected:
        [[nodiscard]] expected<void> runLoad() override
        {
            if ( SharedAssetCache<TAsset>::loadIntoTable( getPath(), _pAsset ) )
                return {};
            return unexpected( Error{ ErrorKind::InvalidArgument, "the asset could not be read (the loader logged why)", getPath() } );
        }

    private:
        friend class SharedAssetTable<TAsset>;

        shared_ptr<const TAsset> _pAsset;
    };
} // namespace sw
