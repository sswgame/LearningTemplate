#include "pch.h"

#include "Editor/Common/GUI/EditorThumbnailCache.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Resource/AssetManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorThumbnailCacheInternal
        {
            static TextureCache* findTextureCache()
            {
                AssetManager* pAssets = editor::getService<AssetManager>();
                return pAssets != nullptr ? &pAssets->getTextureCache() : nullptr;
            }

            static IImGuiRendererBackend* findRendererBackend()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? pContext->getRendererBackend() : nullptr;
            }

            static IRHIDevice* findDevice()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? pContext->getRHIDevice() : nullptr;
            }

            static bool contains( const vector<string>& listID, const string& resourceID )
            {
                return std::find( listID.begin(), listID.end(), resourceID ) != listID.end();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorThumbnailCache::EditorThumbnailCache()
        : _listEntry{}
        , _listPendingID{}
        , _listFailedID{}
        , _frame{ 0 }
    {
    }

    EditorThumbnailCache::~EditorThumbnailCache()
    {
        clear();
    }

    void EditorThumbnailCache::releaseEntry( Entry& entry, IRHIDevice* pDevice )
    {
        IImGuiRendererBackend* pBackend = EditorThumbnailCacheInternal::findRendererBackend();
        if ( entry._pTextureID != nullptr && pBackend != nullptr )
            pBackend->unregisterTexture( entry._pTextureID );
        entry._pTextureID       = nullptr;
        TextureCache* pTextures = EditorThumbnailCacheInternal::findTextureCache();
        if ( entry._textureHandle != 0 && pTextures != nullptr && pDevice != nullptr )
            pTextures->release( entry._resourceID, pDevice );
        entry._textureHandle = 0;
    }

    void EditorThumbnailCache::clear()
    {
        IRHIDevice* pDevice = EditorThumbnailCacheInternal::findDevice();
        for ( Entry& entry : _listEntry )
        {
            releaseEntry( entry, pDevice );
        }
        _listEntry.clear();
        _listPendingID.clear();
        _listFailedID.clear();
    }

    void* EditorThumbnailCache::findTexture( string_view resourceID )
    {
        const string key = FileUtil::normalizePath( resourceID );
        for ( Entry& entry : _listEntry )
        {
            if ( entry._resourceID != key )
                continue;
            entry._lastUsedFrame = _frame;
            return entry._pTextureID;
        }
        const bool bKnown = EditorThumbnailCacheInternal::contains( _listPendingID, key ) || EditorThumbnailCacheInternal::contains( _listFailedID, key );
        if ( bKnown == false )
            _listPendingID.push_back( key );
        return nullptr;
    }

    void EditorThumbnailCache::update()
    {
        ++_frame;
        IRHIDevice* pDevice = EditorThumbnailCacheInternal::findDevice();
        if ( pDevice == nullptr || EditorThumbnailCacheInternal::findRendererBackend() == nullptr )
            return;

        // 다시 읽기 · 디바이스 교체로 텍스처가 바뀐 썸네일은 놓고 다음 그리기가 다시 예약한다.
        const TextureCache* pTextures = EditorThumbnailCacheInternal::findTextureCache();
        for ( size_t entryIndex = _listEntry.size(); entryIndex > 0; --entryIndex )
        {
            Entry&           entry    = _listEntry[entryIndex - 1];
            const Texture2D* pTexture = pTextures != nullptr ? pTextures->find( entry._resourceID ) : nullptr;
            const bool       bValid   = pTexture != nullptr && pTexture->isRHIValid() && pTexture->getHandle() == entry._textureHandle;
            if ( bValid )
                continue;
            releaseEntry( entry, pDevice );
            _listEntry.erase( _listEntry.begin() + static_cast<std::ptrdiff_t>( entryIndex - 1 ) );
        }

        loadPending( pDevice );
        evictOverCapacity( pDevice );
    }

    void EditorThumbnailCache::loadPending( IRHIDevice* pDevice )
    {
        if ( _listPendingID.empty() )
            return;
        const string resourceID = _listPendingID.front();
        _listPendingID.erase( _listPendingID.begin() );

        TextureCache*          pTextures = EditorThumbnailCacheInternal::findTextureCache();
        IImGuiRendererBackend* pBackend  = EditorThumbnailCacheInternal::findRendererBackend();
        Texture2D*             pTexture  = pTextures != nullptr ? pTextures->acquire( resourceID, pDevice ) : nullptr;
        if ( pTexture == nullptr || pTexture->isRHIValid() == false || pBackend == nullptr )
        {
            if ( pTexture != nullptr && pTextures != nullptr )
                pTextures->release( resourceID, pDevice );
            _listFailedID.push_back( resourceID );
            return;
        }

        Entry entry{};
        entry._resourceID    = resourceID;
        entry._textureHandle = pTexture->getHandle();
        entry._pTextureID    = pBackend->registerTexture( entry._textureHandle );
        entry._lastUsedFrame = _frame;
        if ( entry._pTextureID == nullptr )
        {
            pTextures->release( resourceID, pDevice );
            _listFailedID.push_back( resourceID );
            return;
        }
        _listEntry.push_back( std::move( entry ) );
    }

    void EditorThumbnailCache::evictOverCapacity( IRHIDevice* pDevice )
    {
        while ( _listEntry.size() > kCapacity )
        {
            auto itOldest = std::min_element( _listEntry.begin(), _listEntry.end(),
                                              []( const Entry& left, const Entry& right )
            { return left._lastUsedFrame < right._lastUsedFrame; } );
            // 이번 프레임에 그린 것은 놓지 않는다 — 화면에 보이는 수가 용량보다 많으면 잠시 넘친다.
            if ( itOldest == _listEntry.end() || itOldest->_lastUsedFrame + 1 >= _frame )
                return;
            releaseEntry( *itOldest, pDevice );
            _listEntry.erase( itOldest );
        }
    }
} // namespace sw::editor
