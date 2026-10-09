#include "pch.h"

#include "Engine/Resource/SpriteClipCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Resource/SharedAssetTable.h"

namespace sw
{
    namespace
    {
        struct SpriteClipCacheInternal
        {
            [[nodiscard]] static bool loadClip( string_view path, SpriteClipAsset& outClip ) { return outClip.loadFromFile( path ); }

            /** @brief 프로세스에 하나인 공유 표입니다(Engine.dll 안 — 모듈 핫 리로드에 사라지지 않습니다). 쥔 쪽에는 const 로 준다 — 고치는 것은 `reloadShared` 뿐이다. */
            static SharedAssetTable<SpriteClipAsset>& getSharedTable()
            {
                static SharedAssetTable<SpriteClipAsset> s_table;
                return s_table;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<const SpriteClipAsset> SpriteClipCache::acquire( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        return SpriteClipCacheInternal::getSharedTable().acquire( path, &SpriteClipCacheInternal::loadClip );
    }

    bool SpriteClipCache::reloadShared( string_view path )
    {
        if ( path.empty() )
            return false;
        // 읽기에 실패하면 옛 내용을 지킨다(반쯤 쓴 파일을 저장 중에 본 경우 — 다음 이벤트가 다시 읽는다).
        return SpriteClipCacheInternal::getSharedTable().reloadShared( path, &SpriteClipCacheInternal::loadClip );
    }

    bool SpriteClipCache::isCached( string_view relativePath ) const
    {
        return SpriteClipCacheInternal::getSharedTable().findLive( relativePath ) != nullptr;
    }

    void SpriteClipCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 쥔 쪽이 없으면 다시 읽을 것이 없다 — 다음에 읽는 쪽이 새 내용을 읽는다
    }

    size_t SpriteClipCache::getCachedCount() const
    {
        return SpriteClipCacheInternal::getSharedTable().countLive();
    }

    void SpriteClipCache::clear()
    {
        SpriteClipCacheInternal::getSharedTable().clear();
    }
} // namespace sw
