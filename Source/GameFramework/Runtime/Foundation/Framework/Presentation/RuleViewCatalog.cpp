#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Presentation/RuleViewCatalog.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    vector<string> RuleViewCatalog::makeAllMeshPaths() const
    {
        vector<string> listPath;
        for ( const RuleViewDef& view : getAll() )
        {
            if ( VectorUtil::contains( listPath, view._mesh.path() ) == false )
                listPath.push_back( view._mesh.path() );
        }
        return listPath;
    }

    uint32 RuleViewCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<RuleViewCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&RuleViewCatalog::loadView>( "View" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool RuleViewCatalog::loadView( const XMLNode& node, string_view sourceName )
    {
        // 세 칸은 리플렉션 필수 칸이다 — 빠진 칸 · 빈 이름(XMLFields.RequiredFieldMissing) · 범위 밖 폭(XMLFields.ValueOutOfRange)을 이름과 함께 알린다.
        RuleViewDef view;
        if ( XMLFields::readProperties( node, view, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "RuleViewCatalog.ViewInvalidFieldsSkipped", "%#: view '%#' has invalid fields - skipped", sourceName, view._id.view() );
            return false;
        }
        (void)add( view ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<RuleViewCatalog>::acquire`). */
    SW_MODULE_SHARED_ASSET_CACHE( RuleViews, RuleViewCatalog, SW_GF_API, RuleViewCatalog::kAssetType.withLoader( AssetTypeLoader::of<RuleViewCatalog>( MemoryTag::Game ) ) );
} // namespace sw
