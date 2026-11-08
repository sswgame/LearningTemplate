#include "pch.h"

#include "Engine/UI/Style/UIStyleSheetCache.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/UI/Style/UIStyleSheet.h"

namespace sw
{
    namespace
    {
        expected<shared_ptr<const void>> parseSheet( string_view text, string_view path, const ResourceContext& context )
        {
            shared_ptr<UIStyleSheetAsset> sheet = make_shared<UIStyleSheetAsset>();
            if ( expected<void> parsed = UIStyleSheetAsset::parse( text, path, context, *sheet ); parsed.has_value() == false )
                return unexpected( parsed.error() );
            return shared_ptr<const void>( std::move( sheet ) );
        }
    } // namespace
} // namespace sw

namespace sw
{
    UIStyleSheetCache::UIStyleSheetCache()
        : _cache{ "UIStyleSheet", "UI style sheet", &parseSheet }
        , _assetCacheRegistration{}
    {
    }

    UIStyleSheetCache::~UIStyleSheetCache() = default;

    SubsystemInitResult UIStyleSheetCache::initializeSubsystem()
    {
        _assetCacheRegistration = engine::getSubsystem<AssetManager>().registerAssetCache( &_cache, SW_ASSET_CACHE_RELOAD_FUNCTION( UITextAssetCache ) );
        return SubsystemInitResult::Succeeded;
    }

    void UIStyleSheetCache::shutdownSubsystem()
    {
        _assetCacheRegistration.reset();
        _cache.clear();
    }

    span<const utf8* const> UIStyleSheetCache::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName };
        return kArrDependency;
    }

    expected<shared_ptr<const UIStyleSheetAsset>> UIStyleSheetCache::findOrLoad( string_view path )
    {
        return findOrLoad( path, ResourceTagSet{} );
    }

    expected<shared_ptr<const UIStyleSheetAsset>> UIStyleSheetCache::findOrLoad( string_view path, const ResourceTagSet& tagOverride )
    {
        return _cache.findOrLoadAsset( path, tagOverride ).transform( []( const shared_ptr<const void>& pAsset )
        { return std::static_pointer_cast<const UIStyleSheetAsset>( pAsset ); } );
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( UIStyleSheetCache, HostOnly, Startup, (), SW_ENGINE_DEFAULT_FACTORY( UIStyleSheetCache ) );
} // namespace sw
