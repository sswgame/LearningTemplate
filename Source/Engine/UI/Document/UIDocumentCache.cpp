#include "pch.h"

#include "Engine/UI/Document/UIDocumentCache.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentLoader.h"

namespace sw
{
    namespace
    {
        expected<shared_ptr<const void>> parseDocument( string_view text, string_view path, const ResourceContext& context )
        {
            shared_ptr<UIDocumentAsset> document = make_shared<UIDocumentAsset>();
            if ( expected<void> parsed = UIDocumentLoader::parse( text, path, context, *document ); parsed.has_value() == false )
                return unexpected( parsed.error() );
            return shared_ptr<const void>( std::move( document ) );
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

    expected<shared_ptr<const UIDocumentAsset>> UIDocumentCache::findOrLoad( string_view path )
    {
        return findOrLoad( path, ResourceTagSet{} );
    }

    expected<shared_ptr<const UIDocumentAsset>> UIDocumentCache::findOrLoad( string_view path, const ResourceTagSet& tagOverride )
    {
        return _cache.findOrLoadAsset( path, tagOverride ).transform( []( const shared_ptr<const void>& pAsset )
        { return std::static_pointer_cast<const UIDocumentAsset>( pAsset ); } );
    }
} // namespace sw

namespace sw
{
    // UI 문서 · 스타일 시트 캐시 — 화면 스택 · 오프스크린 화면 · 테마 · 에디터 미리보기가 같이 읽는 에셋 캐시라 화면 스택 앞에 따로 선다.
    SW_ENGINE_SERVICE_SUBSYSTEM( UIDocumentCache, HostOnly, Startup, (), SW_ENGINE_DEFAULT_FACTORY( UIDocumentCache ) );
} // namespace sw
