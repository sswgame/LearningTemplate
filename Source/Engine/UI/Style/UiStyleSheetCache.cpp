#include "pch.h"

#include "Engine/UI/Style/UiStyleSheetCache.h"

#include "Engine/UI/Style/UiStyleSheet.h"

namespace sw
{
    namespace
    {
        struct UiStyleSheetCacheInternal
        {
            static shared_ptr<const void> parseSheet( string_view text, string_view path, string& outError )
            {
                shared_ptr<UiStyleSheetAsset> sheet = make_shared<UiStyleSheetAsset>();
                if ( UiStyleSheetLoader::parse( text, path, *sheet, outError ) == false )
                    return {};
                return sheet;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiStyleSheetCache::UiStyleSheetCache()
        : UiTextAssetCache{ "UiStyleSheet", "UI style sheet", &UiStyleSheetCacheInternal::parseSheet }
    {
    }

    UiStyleSheetCache::~UiStyleSheetCache() = default;

    shared_ptr<const UiStyleSheetAsset> UiStyleSheetCache::findOrLoad( string_view path, string& outError )
    {
        return std::static_pointer_cast<const UiStyleSheetAsset>( findOrLoadAsset( path, outError ) );
    }
} // namespace sw
