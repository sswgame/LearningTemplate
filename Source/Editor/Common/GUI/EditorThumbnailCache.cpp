#include "pch.h"

#include "Editor/Common/GUI/EditorThumbnailCache.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
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
                AssetManager* pAssets = editor::findSubsystem<AssetManager>();
                return pAssets != nullptr ? &pAssets->getAssetCache<TextureCache>() : nullptr;
            }

            static IImGuiRendererBackend* findRendererBackend()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? pContext->getSubsystem<EditorRenderViews>().getRendererBackend() : nullptr;
            }

            static IRHIDevice* findDevice()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? pContext->getSubsystem<EditorRenderViews>().getRHIDevice() : nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorThumbnailCache::EditorThumbnailCache()
        : _listEntry{}
        , _listPendingID{}
        , _listLoading{}
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
        entry._pTextureID                = nullptr;
        TextureCache* pTextures          = EditorThumbnailCacheInternal::findTextureCache();
        const bool    bReleasableTexture = entry._textureHandle != 0 && pTextures != nullptr && pDevice != nullptr;
        if ( bReleasableTexture )
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
        _listLoading.clear(); // 핸들만 놓는다 — 워커가 읽던 로드는 엔진이 끝내고, 참조를 쥐었다면 블록이 사라질 때 스스로 놓는다
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
        bool bKnown = VectorUtil::contains( _listPendingID, key ) || VectorUtil::contains( _listFailedID, key );
        for ( const Loading& loading : _listLoading )
        {
            bKnown = bKnown || loading._resourceID == key;
        }
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

        startLoads( pDevice );
        finishLoads( pDevice );
        evictOverCapacity( pDevice );
    }

    void EditorThumbnailCache::startLoads( IRHIDevice* pDevice )
    {
        TextureCache* pTextures = EditorThumbnailCacheInternal::findTextureCache();
        while ( _listPendingID.empty() == false && _listLoading.size() < kMaxLoading )
        {
            const string resourceID = _listPendingID.front();
            _listPendingID.erase( _listPendingID.begin() );

            // 디스크 읽기와 디코드는 워커에서 한다. 이미 올라가 있는 텍스처는 준비된 핸들이 바로 온다.
            const TextureHandle handle = pTextures != nullptr ? pTextures->request( resourceID, pDevice, AssetLoadStart::Async ) : TextureHandle{};
            if ( handle.isValid() == false )
            {
                _listFailedID.push_back( resourceID );
                continue;
            }
            _listLoading.push_back( Loading{ resourceID, handle.getBlock() } );
        }
    }

    void EditorThumbnailCache::finishLoads( IRHIDevice* pDevice )
    {
        uint32 uploadedCount = 0;
        for ( size_t loadingIndex = 0; loadingIndex < _listLoading.size(); )
        {
            TextureLoadBlock& block = *_listLoading[loadingIndex]._pBlock;
            // 워커가 읽기를 마친 로드는 여기서 올린다 — 이 스레드가 디바이스를 쓰는 스레드다(예전 `acquire` 와 같은 자리). 다른 스레드가 이미 집었으면 false 다.
            const bool bMayUpload = block.getState() == AssetLoadState::Loading && uploadedCount < kMaxUploadsPerUpdate;
            if ( bMayUpload && block.runCompletionIfPending() )
                ++uploadedCount;

            const AssetLoadState state = block.getState();
            if ( state == AssetLoadState::Loading )
            {
                ++loadingIndex;
                continue;
            }
            const string resourceID = _listLoading[loadingIndex]._resourceID;
            if ( state == AssetLoadState::Failed || adoptReady( resourceID, pDevice ) == false )
                _listFailedID.push_back( resourceID );
            // 핸들 몫의 참조는 지우며 놓인다 — `adoptReady` 가 우리 몫을 따로 쥐었으므로 항목은 남는다.
            _listLoading.erase( _listLoading.begin() + static_cast<std::ptrdiff_t>( loadingIndex ) );
        }
    }

    bool EditorThumbnailCache::adoptReady( const string& resourceID, IRHIDevice* pDevice )
    {
        TextureCache*          pTextures = EditorThumbnailCacheInternal::findTextureCache();
        IImGuiRendererBackend* pBackend  = EditorThumbnailCacheInternal::findRendererBackend();
        // 준비된 텍스처라 읽지 않는다(캐시 적중) — 핸들이 살아 있는 동안 참조를 하나 더 세어 해제를 `releaseEntry` 의 `release` 짝으로 맞춘다.
        Texture2D* pTexture           = pTextures != nullptr ? pTextures->acquire( resourceID, pDevice ) : nullptr;
        const bool bThumbnailUnusable = pTexture == nullptr || pTexture->isRHIValid() == false || pBackend == nullptr;
        if ( bThumbnailUnusable )
        {
            if ( pTexture != nullptr && pTextures != nullptr )
                pTextures->release( resourceID, pDevice );
            return false;
        }

        Entry entry{};
        entry._resourceID    = resourceID;
        entry._textureHandle = pTexture->getHandle();
        entry._pTextureID    = pBackend->registerTexture( entry._textureHandle );
        entry._lastUsedFrame = _frame;
        if ( entry._pTextureID == nullptr )
        {
            pTextures->release( resourceID, pDevice );
            return false;
        }
        _listEntry.push_back( std::move( entry ) );
        return true;
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
