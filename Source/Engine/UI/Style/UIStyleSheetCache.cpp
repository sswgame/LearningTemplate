#include "pch.h"

#include "Engine/UI/Style/UIStyleSheetCache.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/UI/Style/UIStyleSheet.h"

namespace sw
{
    namespace
    {
        shared_ptr<const void> parseSheet( string_view text, string_view path, string& outError )
        {
            shared_ptr<UIStyleSheetAsset> sheet = make_shared<UIStyleSheetAsset>();
            if ( UIStyleSheetLoader::parse( text, path, *sheet, outError ) == false )
                return {};
            return sheet;
        }
    } // namespace
} // namespace sw

namespace sw
{
    UIStyleSheetCache::UIStyleSheetCache()
        : UITextAssetCache{ "UIStyleSheet", "UI style sheet", &parseSheet }
        , _assetCacheRegistration{}
    {
    }

    UIStyleSheetCache::~UIStyleSheetCache() = default;

    SubsystemInitResult UIStyleSheetCache::initializeSubsystem()
    {
        _assetCacheRegistration = engine::getSubsystem<AssetManager>().registerAssetCache( this, SW_ASSET_CACHE_RELOAD_FUNCTION( UIStyleSheetCache ) );
        return SubsystemInitResult::Succeeded;
    }

    void UIStyleSheetCache::shutdownSubsystem()
    {
        _assetCacheRegistration.reset();
        clear();
    }

    span<const utf8* const> UIStyleSheetCache::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName };
        return kArrDependency;
    }

    shared_ptr<const UIStyleSheetAsset> UIStyleSheetCache::findOrLoad( string_view path, string& outError )
    {
        return std::static_pointer_cast<const UIStyleSheetAsset>( findOrLoadAsset( path, outError ) );
    }
} // namespace sw
