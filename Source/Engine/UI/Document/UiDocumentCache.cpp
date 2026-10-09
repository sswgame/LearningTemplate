#include "pch.h"

#include "Engine/UI/Document/UiDocumentCache.h"

#include "Engine/UI/Document/UiDocument.h"
#include "Engine/UI/Document/UiDocumentLoader.h"

namespace sw
{
    namespace
    {
        struct UiDocumentCacheInternal
        {
            static shared_ptr<const void> parseDocument( string_view text, string_view path, string& outError )
            {
                shared_ptr<UiDocumentAsset> document = make_shared<UiDocumentAsset>();
                if ( UiDocumentLoader::parse( text, path, *document, outError ) == false )
                    return {};
                return document;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiDocumentCache::UiDocumentCache()
        : UiTextAssetCache{ "UiDocument", "UI document", &UiDocumentCacheInternal::parseDocument }
    {
    }

    UiDocumentCache::~UiDocumentCache() = default;

    shared_ptr<const UiDocumentAsset> UiDocumentCache::findOrLoad( string_view path, string& outError )
    {
        return std::static_pointer_cast<const UiDocumentAsset>( findOrLoadAsset( path, outError ) );
    }
} // namespace sw
