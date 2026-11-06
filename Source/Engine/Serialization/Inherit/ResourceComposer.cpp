#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceComposer.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct ResourceComposerInternal
        {
            /** @brief 읽는 쪽이 아는 규칙으로 펼칩니다. 쿠킹본만 읽는 구성에서 `_base` 가 남아 있으면 오류입니다 — 런타임은 펼친 결과만 읽는다(`PackCooker`). */
            [[nodiscard]] static bool flattenFromResources( string_view text, string_view id, const DataInheritanceSchema& schema, string& outText )
            {
                if ( AssetIO::readsSource() == false )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.NotFlattened", "'%#' still has %# in cooked content - the pack cook flattens it (PackCooker)", id, DataInheritance::kBaseAttribute );
                    return false;
                }
                DataInheritanceResourceSource source;
                return DataInheritance::flatten( text, id, schema, source, outText );
            }

            /**
             * @brief 루트 이름으로 규칙을 찾아 펼칩니다. 문서 표 → 손 형식 → 리플렉션 형식 차례입니다. 셋 다 없으면 오류입니다.
             * @details 문서 표가 먼저다 — 쿠킹은 Engine 의 등록만 보므로, 표는 쿠킹과 개발 실행이 같은 답을 내는 유일한 출처다. 자기 입구가 합치는 형식(`SkeletonVariant`)은 표가 그렇게 적어 글 그대로 담는다.
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
                bool                  bListed   = false;
                bool                  bOwnMerge = false;
                if ( DataCatalogInheritance::loadDocumentRule( rootName, schema, bListed, bOwnMerge ) == false )
                    return false;
                if ( bOwnMerge )
                    return true;
                outbComposed = true;
                if ( bListed )
                    return DataInheritance::flatten( text, id, schema, source, outText );
                const DataInheritance::MakeSchemaFunction pfnMakeSchema = DataInheritanceSchemaRegistrar::find( rootName );
                if ( pfnMakeSchema != nullptr )
                    return DataInheritance::flatten( text, id, pfnMakeSchema(), source, outText );
                const string    rootNameText{ rootName };
                const TypeInfo* pTypeInfo = engine::getSubsystem<TypeRegistry>().findType( hashed_string( rootNameText.c_str() ) );
                if ( pTypeInfo == nullptr )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.NoSchema", "'%#': <%#> has %# but no merge rule the cook can see - add it to listDocument in %# (game and kit types are not loaded while cooking)", id, rootName,
                                     DataInheritance::kBaseAttribute, DataCatalogInheritance::kConfigPath );
                    return false;
                }
                return DataInheritance::flatten( text, id, DataInheritanceSchema::makeFromType( *pTypeInfo ), source, outText );
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
        return ResourceContext{ ParentRule::CatalogTable, nullptr, nullptr, nullptr };
    }

    ResourceContext ResourceContext::forSchema( DataInheritance::MakeSchemaFunction pfnMakeSchema )
    {
        return ResourceContext{ ParentRule::KnownSchema, pfnMakeSchema, nullptr, nullptr };
    }

    ResourceContext ResourceContext::forType( const TypeInfo& typeInfo )
    {
        return ResourceContext{ ParentRule::KnownType, nullptr, &typeInfo, nullptr };
    }

    ResourceContext ResourceContext::forPackCook( IDataInheritanceSource& source )
    {
        return ResourceContext{ ParentRule::RootName, nullptr, nullptr, &source };
    }

    bool ResourceComposer::compose( string_view text, string_view id, const ResourceContext& context, string& outText, bool& outbComposed )
    {
        outbComposed = false;
        return ResourceComposerInternal::composeParents( text, id, context, outText, outbComposed );
    }
} // namespace sw
