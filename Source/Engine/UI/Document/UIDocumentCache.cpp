#include "pch.h"

#include "Engine/UI/Document/UIDocumentCache.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentLoader.h"

namespace sw
{
    namespace
    {
        shared_ptr<const void> parseDocument( string_view text, string_view path, string& outError )
        {
            shared_ptr<UIDocumentAsset> document = make_shared<UIDocumentAsset>();
            if ( UIDocumentLoader::parse( text, path, *document, outError ) == false )
                return {};
            return document;
        }
    } // namespace
} // namespace sw

namespace sw
{
    UIDocumentCache::UIDocumentCache()
        : _cache{ "UIDocument", "UI document", &parseDocument }
        , _assetCacheRegistration{}
    {
    }

    UIDocumentCache::~UIDocumentCache() = default;

    SubsystemInitResult UIDocumentCache::initializeSubsystem()
    {
        // 핫 리로드 · 종료 · 진단이 다른 에셋 캐시와 같은 길로 간다.
        _assetCacheRegistration = engine::getSubsystem<AssetManager>().registerAssetCache( &_cache, SW_ASSET_CACHE_RELOAD_FUNCTION( UITextAssetCache ) );
        return SubsystemInitResult::Succeeded;
    }

    void UIDocumentCache::shutdownSubsystem()
    {
        _assetCacheRegistration.reset();
        _cache.clear();
    }

    span<const utf8* const> UIDocumentCache::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName };
        return kArrDependency;
    }

    shared_ptr<const UIDocumentAsset> UIDocumentCache::findOrLoad( string_view path, string& outError )
    {
        return std::static_pointer_cast<const UIDocumentAsset>( _cache.findOrLoadAsset( path, outError ) );
    }
} // namespace sw
