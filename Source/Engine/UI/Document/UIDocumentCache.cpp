#include "pch.h"

#include "Engine/UI/Document/UIDocumentCache.h"

#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentLoader.h"

namespace sw
{
    namespace
    {
        struct UIDocumentCacheInternal
        {
            static shared_ptr<const void> parseDocument( string_view text, string_view path, string& outError )
            {
                shared_ptr<UIDocumentAsset> document = make_shared<UIDocumentAsset>();
                if ( UIDocumentLoader::parse( text, path, *document, outError ) == false )
                    return {};
                return document;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIDocumentCache::UIDocumentCache()
        : UITextAssetCache{ "UIDocument", "UI document", &UIDocumentCacheInternal::parseDocument }
    {
    }

    UIDocumentCache::~UIDocumentCache() = default;

    shared_ptr<const UIDocumentAsset> UIDocumentCache::findOrLoad( string_view path, string& outError )
    {
        return std::static_pointer_cast<const UIDocumentAsset>( findOrLoadAsset( path, outError ) );
    }
} // namespace sw
