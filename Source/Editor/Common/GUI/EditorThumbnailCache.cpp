#include "pch.h"

#include "Editor/Common/GUI/EditorThumbnailCache.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/LogOnce.h"

#include "Editor/Common/Backend/ImGuiRenderer.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw::editor
{
    namespace
    {
        struct EditorThumbnailCacheInternal
        {
            /** @brief 읽기 · 올리기 경로의 텍스처 캐시입니다. 에디터가 프레임을 도는 동안은 늘 있다. */
            static TextureCache& requireTextureCache() { return editor::getSubsystem<AssetManager>().getAssetCache<TextureCache>(); }

            /** @brief 해제 경로(소멸자 · 종료)의 텍스처 캐시입니다. 애셋 매니저가 먼저 내려갔으면 텍스처도 함께 사라졌으니 놓을 것이 없다. */
            static TextureCache* findTextureCache()
            {
                AssetManager* pAssets = editor::findSubsystem<AssetManager>(); // 종료 순서에 따라 이미 없을 수 있다
                return pAssets != nullptr ? &pAssets->getAssetCache<TextureCache>() : nullptr;
            }

            static ImGuiRenderer* findRenderer()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? pContext->getSubsystem<EditorRenderViews>().getRenderer() : nullptr;
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
        , _uniqueKnownID{}
        , _frame{ 0 }
    {
    }

    EditorThumbnailCache::~EditorThumbnailCache()
    {
        clear();
    }

    void EditorThumbnailCache::releaseEntry( Entry& entry )
    {
        ImGuiRenderer* pBackend = EditorThumbnailCacheInternal::findRenderer();
        if ( entry._pTextureID != nullptr && pBackend != nullptr )
            pBackend->unregisterTexture( entry._pTextureID );
        entry._pTextureID    = nullptr;
        entry._pOperation    = nullptr; // 쥔 참조를 놓는다 — 마지막이면 엔진이 텍스처를 놓는다
        entry._textureHandle = 0;
    }

    void EditorThumbnailCache::clear()
    {
        for ( Entry& entry : _listEntry )
        {
            releaseEntry( entry );
        }
        _listEntry.clear();
        _listPendingID.clear();
        _listLoading.clear(); // 핸들만 놓는다 — 워커가 읽던 로드는 엔진이 끝내고, 참조를 쥐었다면 로드 작업이 사라질 때 스스로 놓는다
        _listFailedID.clear();
        _uniqueKnownID.clear();
    }

    void EditorThumbnailCache::recordFailure( string assetID )
    {
        _listFailedID.push_back( std::move( assetID ) );
        while ( _listFailedID.size() > kMaxFailed )
        {
            _uniqueKnownID.erase( _listFailedID.front() ); // 잊힌 경로는 다시 보이면 새로 예약된다
            _listFailedID.pop_front();
        }
    }

    void* EditorThumbnailCache::findTexture( string_view assetID )
    {
        const string key = FileUtil::normalizePath( assetID ).str();
        for ( Entry& entry : _listEntry )
        {
            if ( entry._assetID != key )
                continue;
            entry._lastUsedFrame = _frame;
            return entry._pTextureID;
        }
        // 예약 · 읽는 중 · 실패는 한 집합에 있다 — 프레임마다 보이는 모든 칸이 부르므로 목록을 훑지 않는다.
        if ( _uniqueKnownID.insert( key ).second )
            _listPendingID.push_back( key );
        return nullptr;
    }

    void EditorThumbnailCache::update()
    {
        ++_frame;
        if ( EditorThumbnailCacheInternal::findRenderer() == nullptr )
            return;

        // 다시 읽기 · 디바이스 교체로 텍스처가 바뀐 썸네일은 놓고 다음 그리기가 다시 예약한다.
        const TextureCache& textures = EditorThumbnailCacheInternal::requireTextureCache();
        for ( size_t entryIndex = _listEntry.size(); entryIndex > 0; --entryIndex )
        {
            Entry&           entry    = _listEntry[entryIndex - 1];
            const Texture2D* pTexture = textures.find( entry._assetID );
            const bool       bValid   = pTexture != nullptr && pTexture->isRHIValid() && pTexture->getHandle() == entry._textureHandle;
            if ( bValid )
                continue;
            releaseEntry( entry );
            _listEntry.erase( _listEntry.begin() + static_cast<std::ptrdiff_t>( entryIndex - 1 ) );
        }

        startLoads();
        finishLoads();
        evictOverCapacity();
    }

    void EditorThumbnailCache::startLoads()
    {
        if ( _listPendingID.empty() )
            return;
        TextureCache& textures = EditorThumbnailCacheInternal::requireTextureCache();
        while ( _listPendingID.empty() == false && _listLoading.size() < kMaxLoading )
        {
            const string assetID = std::move( _listPendingID.front() );
            _listPendingID.pop_front();

            // 디스크 읽기와 디코드는 워커에서 한다. 이미 올라가 있는 텍스처는 준비된 핸들이 바로 온다.
            const TextureHandle handle = textures.request( assetID, AssetLoadStart::Async );
            if ( handle.isValid() == false )
            {
                recordFailure( assetID );
                continue;
            }
            _listLoading.push_back( Loading{ assetID, handle.getOperation() } );
        }
    }

    void EditorThumbnailCache::finishLoads()
    {
        for ( size_t loadingIndex = 0; loadingIndex < _listLoading.size(); )
        {
            const TextureLoadOperation& operation = *_listLoading[loadingIndex]._pOperation;
            // 올리기는 렌더 쪽 로드 펌프가 한다 — 여기서는 끝났는지만 본다.
            const AssetLoadState state = operation.getState();
            if ( state == AssetLoadState::Loading )
            {
                ++loadingIndex;
                continue;
            }
            const string assetID = _listLoading[loadingIndex]._assetID;
            if ( state == AssetLoadState::Failed || applyReady( assetID, _listLoading[loadingIndex]._pOperation ) == false )
                recordFailure( assetID );
            else
                _uniqueKnownID.erase( assetID ); // 항목이 생겼다 — 이후엔 항목 목록이 답한다. 쫓겨나면 다시 예약할 수 있게 비운다
            // 로드 목록에서 지워도 항목이 로드 작업을 쥐었으므로(`applyReady`) 참조는 남는다.
            _listLoading.erase( _listLoading.begin() + static_cast<std::ptrdiff_t>( loadingIndex ) );
        }
    }

    bool EditorThumbnailCache::applyReady( const string& assetID, const shared_ptr<TextureLoadOperation>& pOperation )
    {
        const TextureCache& textures = EditorThumbnailCacheInternal::requireTextureCache();
        ImGuiRenderer*      pBackend = EditorThumbnailCacheInternal::findRenderer();
        // 끝난 로드가 참조를 쥐고 있다 — 다시 빌리지 않고(`acquire` 는 디바이스 스레드의 일) 그 로드 작업을 항목이 쥔다.
        const Texture2D* pTexture           = textures.find( assetID );
        const bool       bThumbnailUnusable = pTexture == nullptr || pTexture->isRHIValid() == false || pBackend == nullptr;
        if ( bThumbnailUnusable )
            return false;

        Entry entry{};
        entry._assetID       = assetID;
        entry._pOperation    = pOperation;
        entry._textureHandle = pTexture->getHandle();
        entry._pTextureID    = pBackend->registerTexture( entry._textureHandle );
        entry._lastUsedFrame = _frame;
        if ( entry._pTextureID == nullptr )
            return false;
        _listEntry.push_back( std::move( entry ) );
        return true;
    }

    void EditorThumbnailCache::evictOverCapacity()
    {
        while ( _listEntry.size() > kCapacity )
        {
            auto itOldest = std::min_element( _listEntry.begin(), _listEntry.end(),
                                              []( const Entry& left, const Entry& right )
            { return left._lastUsedFrame < right._lastUsedFrame; } );
            // 이번 프레임에 그린 것은 놓지 않는다 — 화면에 보이는 수가 용량보다 많으면 잠시 넘친다.
            if ( itOldest == _listEntry.end() || itOldest->_lastUsedFrame + 1 >= _frame )
                return;
            releaseEntry( *itOldest );
            _listEntry.erase( itOldest );
        }
    }
} // namespace sw::editor
