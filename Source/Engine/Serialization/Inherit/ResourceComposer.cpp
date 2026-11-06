#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceComposer.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct ResourceComposerInternal
        {
            /** @brief 쿠킹본만 읽는 구성에서 `_base` 가 남아 있으면 오류를 남기고 false 입니다 — 런타임은 펼친 결과만 읽는다(`PackCooker`). */
            [[nodiscard]] static bool canFlattenAtRuntime( string_view id )
            {
                if ( AssetIO::readsSource() )
                    return true;
                SW_LOG_ERROR_ID( "DataInheritance.NotFlattened", "'%#' still has %# in cooked content - the pack cook flattens it (PackCooker)", id, DataInheritance::kBaseAttribute );
                return false;
            }

            /** @brief 읽는 쪽이 아는 규칙으로 펼칩니다. */
            [[nodiscard]] static bool flattenFromResources( string_view text, string_view id, const DataInheritanceSchema& schema, string& outText )
            {
                if ( canFlattenAtRuntime( id ) == false )
                    return false;
                DataInheritanceResourceSource source;
                return DataInheritance::flatten( text, id, schema, source, outText );
            }

            /**
             * @brief 루트 이름으로 규칙을 찾아 펼칩니다. 문서 표 → 합치기 함수 → 손 형식 차례입니다. 모두 없으면 오류입니다.
             * @details 리플렉션 형식의 규칙은 문서 표에만 있다 — 쿠커는 대상마다 지어진 형식이 다르고(서버는 Renderer 를 짓지 않는다) 게임 모듈을 올리기 전에 돈다. 표가 코드와 같은지는 `ResourceComposerTest` 가 본다.
             */
            [[nodiscard]] static bool flattenByRootName( string_view text, string_view id, IDataInheritanceSource& source, string& outText, bool& outbComposed )
            {
                XMLDocument doc;
                if ( doc.parse( text, id ) == false || doc.getRoot().isValid() == false )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.NotXML2", "'%#' could not be parsed: %#", id, doc.getLastError() );
                    return false;
                }
                const string_view     rootName = doc.getRoot().getName();
                DataInheritanceSchema schema{ rootName };
                bool                  bListed = false;
                if ( DataCatalogInheritance::loadDocumentRule( rootName, schema, bListed ) == false )
                    return false;
                outbComposed = true;
                if ( bListed )
                    return DataInheritance::flatten( text, id, schema, source, outText );
                const DataInheritance::MergeFunction pfnMerge = DataInheritanceSchemaRegistrar::findMerge( rootName );
                if ( pfnMerge != nullptr )
                    return pfnMerge( text, id, source, outText );
                const DataInheritance::MakeSchemaFunction pfnMakeSchema = DataInheritanceSchemaRegistrar::find( rootName );
                if ( pfnMakeSchema != nullptr )
                    return DataInheritance::flatten( text, id, pfnMakeSchema(), source, outText );
                // 리플렉션 형식은 보지 않는다 — 쿠킹 프로세스에 어떤 형식이 지어졌는지(서버는 Renderer 가 없다 · 게임 모듈은 아직 없다)에 쿠킹본이 갈리면 안 된다.
                SW_LOG_ERROR_ID( "DataInheritance.NoSchema", "'%#': <%#> has %# but no merge rule the cook can see - add it to listDocument in %# (the cook does not read reflection)", id, rootName,
                                 DataInheritance::kBaseAttribute, DataCatalogInheritance::kConfigPath );
                return false;
            }

            /** @brief 부모 단계입니다. */
            [[nodiscard]] static bool composeParents( string_view text, string_view id, const ResourceContext& context, string& outText, bool& outbComposed )
            {
                switch ( context._parentRule )
                {
                    case ResourceContext::ParentRule::CatalogTable:
                    {
                        // 쿠킹본만 읽는 구성은 펼친 결과만 있으므로 보지 않는다(남은 `_base` 는 읽기의 모르는 칸 오류다).
                        if ( text.find( DataInheritance::kBaseAttribute ) == string_view::npos || AssetIO::readsSource() == false )
                            return true;
                        DataInheritanceResourceSource source;
                        return DataCatalogInheritance::flattenIfListed( text, id, source, outbComposed, outText );
                    }
                    case ResourceContext::ParentRule::KnownSchema:
                        if ( DataInheritance::findBasePath( text ).empty() )
                            return true;
                        outbComposed = true;
                        return flattenFromResources( text, id, context._pfnMakeSchema(), outText );
                    case ResourceContext::ParentRule::KnownType:
                        if ( DataInheritance::findBasePath( text ).empty() )
                            return true;
                        outbComposed = true;
                        return flattenFromResources( text, id, DataInheritanceSchema::makeFromType( *context._pTypeInfo ), outText );
                    case ResourceContext::ParentRule::KnownMerge:
                    {
                        if ( DataInheritance::findBasePath( text ).empty() )
                            return true;
                        outbComposed = true;
                        if ( canFlattenAtRuntime( id ) == false )
                            return false;
                        DataInheritanceResourceSource source;
                        return context._pfnMerge( text, id, source, outText );
                    }
                    case ResourceContext::ParentRule::RootName:
                        if ( DataCatalogInheritance::flattenIfListed( text, id, *context._pSource, outbComposed, outText ) == false )
                            return false;
                        if ( outbComposed || DataInheritance::findBasePath( text ).empty() )
                            return true;
                        return flattenByRootName( text, id, *context._pSource, outText, outbComposed );
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ResourceContext ResourceContext::forDocument()
    {
        return ResourceContext{ ParentRule::CatalogTable, nullptr, nullptr, nullptr, nullptr };
    }

    ResourceContext ResourceContext::forSchema( DataInheritance::MakeSchemaFunction pfnMakeSchema )
    {
        return ResourceContext{ ParentRule::KnownSchema, pfnMakeSchema, nullptr, nullptr, nullptr };
    }

    ResourceContext ResourceContext::forType( const TypeInfo& typeInfo )
    {
        return ResourceContext{ ParentRule::KnownType, nullptr, &typeInfo, nullptr, nullptr };
    }

    ResourceContext ResourceContext::forMerge( DataInheritance::MergeFunction pfnMerge )
    {
        return ResourceContext{ ParentRule::KnownMerge, nullptr, nullptr, pfnMerge, nullptr };
    }

    ResourceContext ResourceContext::forPackCook( IDataInheritanceSource& source )
    {
        return ResourceContext{ ParentRule::RootName, nullptr, nullptr, nullptr, &source };
    }

    bool ResourceComposer::compose( string_view text, string_view id, const ResourceContext& context, string& outText, bool& outbComposed )
    {
        outbComposed = false;
        return ResourceComposerInternal::composeParents( text, id, context, outText, outbComposed );
    }
} // namespace sw
