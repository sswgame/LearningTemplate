#include "pch.h"

#include "Engine/UI/Style/UIStyleSheetCache.h"

#include "Engine/UI/Style/UIStyleSheet.h"

namespace sw
{
    namespace
    {
        struct UIStyleSheetCacheInternal
        {
            static shared_ptr<const void> parseSheet( string_view text, string_view path, string& outError )
            {
                shared_ptr<UIStyleSheetAsset> sheet = make_shared<UIStyleSheetAsset>();
                if ( UIStyleSheetLoader::parse( text, path, *sheet, outError ) == false )
                    return {};
                return sheet;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIStyleSheetCache::UIStyleSheetCache()
        : UITextAssetCache{ "UIStyleSheet", "UI style sheet", &UIStyleSheetCacheInternal::parseSheet }
    {
    }

    UIStyleSheetCache::~UIStyleSheetCache() = default;

    shared_ptr<const UIStyleSheetAsset> UIStyleSheetCache::findOrLoad( string_view path, string& outError )
    {
        return std::static_pointer_cast<const UIStyleSheetAsset>( findOrLoadAsset( path, outError ) );
    }
} // namespace sw
