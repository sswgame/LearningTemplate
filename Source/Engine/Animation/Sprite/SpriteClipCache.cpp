#include "pch.h"

#include "Engine/Animation/Sprite/SpriteClipCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/SharedAssetTable.h"

namespace sw
{
    namespace
    {
        struct SpriteClipCacheInternal
        {
            [[nodiscard]] static bool loadClip( string_view path, SpriteClipAsset& outClip ) { return outClip.loadFromFile( path ); }

            /** @brief 프로세스에 하나인 공유 테이블입니다(Engine.dll 안에 있어 모듈 핫 리로드에도 사라지지 않습니다). 참조를 가진 쪽에는 const 로 줍니다. 고치는 것은 `reloadShared` 뿐입니다. */
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
        // 읽기에 실패하면 이전 내용을 유지한다(반쯤 쓴 파일을 저장 중에 본 경우이며, 다음 이벤트가 다시 읽는다).
        return SpriteClipCacheInternal::getSharedTable().reloadShared( path, &SpriteClipCacheInternal::loadClip );
    }

    bool SpriteClipCache::isCached( string_view relativePath ) const
    {
        return SpriteClipCacheInternal::getSharedTable().findLive( relativePath ) != nullptr;
    }

    void SpriteClipCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        (void)reloadShared( relativePath ); // 참조를 가진 쪽이 없으면 다시 읽을 것이 없다. 다음에 읽는 쪽이 새 내용을 읽는다.
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

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( SpriteClipCache );
} // namespace sw
