#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceComposer.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        /** @brief 층 파일의 암묵적 부모(@p baseID)는 지금까지 합친 글로 답하고, 나머지 부모(다른 파일의 카탈로그 항목)는 리소스로 읽습니다. */
        class ResourceLayerBaseSource final : public IDataInheritanceSource
        {
        public:
            ResourceLayerBaseSource( string_view baseID, string_view baseText )
                : _baseID{ baseID }
                , _baseText{ baseText }
            {
            }

            [[nodiscard]] bool readText( string_view assetID, string& outText ) override
            {
                if ( assetID == _baseID )
                {
                    outText = _baseText;
                    return true;
                }
                return AssetIO::readTextAsset( AssetID{ assetID }, outText );
            }

        private:
            string _baseID;
            string _baseText;
        };

        /** @brief 조건 단계입니다. 원소마다 자기 `_when` 을 풀어 속성을 바꾸거나 원소를 빼고, 자식은 위에서 아래로 같은 일을 합니다. */
        struct ResourceConditionStageInternal
        {
            /** @brief 원소 하나의 조건을 푼 결과입니다. */
            struct Decision
            {
                vector<string>  _listOverrideName;  ///< 참인 조건이 바꾼 속성 이름(나중 조건이 이긴다)
                vector<string>  _listOverrideValue; ///< `_listOverrideName` 과 같은 순서의 값
                vector<XMLNode> _listKept;          ///< 남긴 조건 원소(적힌 차례)
                uint8           _bRemoved : 1;      ///< 참인 조건이 원소를 뺐다

                Decision()
                    : _listOverrideName{}
                    , _listOverrideValue{}
                    , _listKept{}
                    , _bRemoved{ SW_FALSE }
                {
                }
            };

            /** @brief 바이너리 · 셰이더를 XML 파서에 넣지 않습니다(팩 쿠커가 모든 파일을 지나 보낸다 — `DataInheritance::findBasePath` 와 같은 까닭). */
            static bool looksLikeXML( string_view text )
            {
                string_view body = StringUtil::trim( text );
                if ( StringUtil::startsWith( body, "\xEF\xBB\xBF" ) )
                    body.remove_prefix( 3 );
                return body.empty() == false && body.front() == '<';
            }

            static bool isElement( const XMLNode& node )
            {
                return node.isValid() && StringUtil::isNullOrEmpty( node.getName() ) == false;
            }

            static bool isWhen( const XMLNode& node )
            {
                return isElement( node ) && string_view( node.getName() ) == DataInheritance::kWhenElement;
            }

            static void setOverride( Decision& inoutDecision, const utf8* pName, const utf8* pValue )
            {
                for ( size_t index = 0; index < inoutDecision._listOverrideName.size(); ++index )
                {
                    if ( inoutDecision._listOverrideName[index] == pName )
                    {
                        inoutDecision._listOverrideValue[index] = pValue;
                        return;
                    }
                }
                inoutDecision._listOverrideName.emplace_back( pName );
                inoutDecision._listOverrideValue.emplace_back( pValue );
            }

            /** @brief @p element 의 조건 원소를 차례로 풉니다. 틀린 조건 원소는 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool decide( const XMLNode& element, string_view id, ResourceTagTiming resolvedTiming, const ResourceTagSet& tags, Decision& outDecision, bool& inoutbChanged,
                                              vector<string>& inoutListLoadTag )
            {
                bool bKeepRest = false; // 이 원소에서 남긴 조건 뒤의 조건은 차례를 지키려 함께 남긴다
                for ( XMLNode when = element.findChild( DataInheritance::kWhenElement ); when.isValid(); when = when.findNextSibling( DataInheritance::kWhenElement ) )
                {
                    for ( XMLNode child = when.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        if ( isElement( child ) )
                        {
                            SW_LOG_ERROR_ID( "ResourceCondition.WhenHasChildren", "'%#': <%#> in <%#> has a child <%#> - a condition sets attributes of the element it is in; put it inside the element it changes",
                                             id, DataInheritance::kWhenElement, element.getName(), child.getName() );
                            return false;
                        }
                    }
                    const utf8* pTest = when.findAttribute( DataInheritance::kIfAttribute );
                    if ( pTest == nullptr )
                    {
                        SW_LOG_ERROR_ID( "ResourceCondition.NoTest", "'%#': <%#> in <%#> has no %#", id, DataInheritance::kWhenElement, element.getName(), DataInheritance::kIfAttribute );
                        return false;
                    }
                    const string_view op      = when.getAttributeText( DataInheritance::kOpAttribute );
                    const bool        bRemove = op == DataInheritance::kOpRemove;
                    if ( op.empty() == false && bRemove == false )
                    {
                        SW_LOG_ERROR_ID( "ResourceCondition.UnknownOp", "'%#': <%#> in <%#> has %#='%#' - only %# is allowed", id, DataInheritance::kWhenElement, element.getName(),
                                         DataInheritance::kOpAttribute, op, DataInheritance::kOpRemove );
                        return false;
                    }
                    bool bSetsValue = false;
                    for ( XMLAttribute attribute = when.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                    {
                        const string_view name = attribute.getName();
                        bSetsValue             = bSetsValue || ( name != DataInheritance::kIfAttribute && name != DataInheritance::kOpAttribute );
                    }
                    if ( bSetsValue == false && bRemove == false )
                    {
                        SW_LOG_ERROR_ID( "ResourceCondition.DoesNothing", "'%#': <%#> in <%#> neither sets a value nor removes the element", id, DataInheritance::kWhenElement, element.getName() );
                        return false;
                    }
                    ResourceTagTest test;
                    if ( ResourceTags::compileTest( pTest, id, test ) == false )
                        return false;
                    for ( const string& tag : test._listLoadTag )
                    {
                        if ( VectorUtil::contains( inoutListLoadTag, tag ) == false )
                            inoutListLoadTag.push_back( tag );
                    }
                    const bool bKeep = resolvedTiming == ResourceTagTiming::Cook && ( bKeepRest || test._timing == ResourceTagTiming::Load );
                    if ( bKeep )
                    {
                        bKeepRest = true;
                        outDecision._listKept.push_back( when );
                        continue;
                    }
                    inoutbChanged = true; // 푼 조건 원소는 결과에서 빠진다
                    if ( test.evaluate( tags ) == false )
                        continue;
                    if ( bRemove )
                        outDecision._bRemoved = SW_TRUE;
                    for ( XMLAttribute attribute = when.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                    {
                        const string_view name = attribute.getName();
                        if ( name != DataInheritance::kIfAttribute && name != DataInheritance::kOpAttribute )
                            setOverride( outDecision, attribute.getName(), attribute.getValue() );
                    }
                }
                return true;
            }

            /** @brief @p src 의 속성 · 글 · 남긴 조건 · 자식(푼 것)을 @p out 에 씁니다. 자식이 빠지면 쓰지 않습니다. */
            [[nodiscard]] static bool writeResolved( const XMLNode& src, const Decision& decision, const XMLNode& out, string_view id, ResourceTagTiming resolvedTiming, const ResourceTagSet& tags,
                                                     bool& inoutbChanged, vector<string>& inoutListLoadTag )
            {
                for ( XMLAttribute attribute = src.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    out.setAttribute( attribute.getName(), attribute.getValue() );
                }
                for ( size_t index = 0; index < decision._listOverrideName.size(); ++index )
                {
                    out.setAttribute( decision._listOverrideName[index].c_str(), string_view( decision._listOverrideValue[index] ) );
                }
                if ( StringUtil::isNullOrEmpty( src.getText() ) == false )
                    out.setValue( src.getText() );
                for ( const XMLNode& kept : decision._listKept )
                {
                    (void)out.appendClone( kept );
                }
                for ( XMLNode child = src.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false || isWhen( child ) )
                        continue;
                    Decision childDecision;
                    if ( decide( child, id, resolvedTiming, tags, childDecision, inoutbChanged, inoutListLoadTag ) == false )
                        return false;
                    if ( childDecision._bRemoved == SW_TRUE )
                        continue;
                    if ( writeResolved( child, childDecision, out.appendChild( child.getName() ), id, resolvedTiming, tags, inoutbChanged, inoutListLoadTag ) == false )
                        return false;
                }
                return true;
            }
        };

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
                    {
                        if ( DataInheritance::findBasePath( text ).empty() )
                            return true;
                        outbComposed = true;
                        // 쿠커와 같은 규칙을 쓴다 — 리플렉션 형식의 규칙은 문서 표에만 있다(쿠커는 리플렉션을 보지 않는다). 표에 없으면 개발 실행도 쿠킹처럼 멈춰야 쿠킹에서야 드러나지 않는다.
                        // 표가 리플렉션과 같은지는 `ResourceComposerTest.DocumentTableMatchesReflection` 이 본다.
                        const utf8*           pRootName = context._pTypeInfo->_name.c_str();
                        DataInheritanceSchema schema{ pRootName };
                        bool                  bListed = false;
                        if ( DataCatalogInheritance::loadDocumentRule( pRootName, schema, bListed ) == false )
                            return false;
                        if ( bListed == false )
                        {
                            SW_LOG_ERROR_ID( "DataInheritance.NoSchema", "'%#': <%#> has %# but no row in listDocument of %# - the cook could not flatten it either", id, pRootName, DataInheritance::kBaseAttribute,
                                             DataCatalogInheritance::kConfigPath );
                            return false;
                        }
                        return flattenFromResources( text, id, schema, outText );
                    }
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

            /**
             * @brief 층 파일 하나(@p layerText, 부모를 적었다)를 @p source 의 암묵적 부모 위에 얹습니다. 규칙은 읽는 쪽의 형식에서 옵니다.
             * @details 리플렉션 형식은 리플렉션에서 규칙을 만든다 — 층은 배포 런타임에서도 얹고(DLC · 모드) 그때 규칙 표(`Config/Dev`)가 없다. 표와 리플렉션이 같은지는
             *          `ResourceComposerTest.DocumentTableMatchesReflection` 이 본다. 카탈로그 표 문맥(`forDocument`)은 표를 읽는다.
             */
            [[nodiscard]] static bool mergeLayer( string_view layerText, string_view layerID, const ResourceContext& context, IDataInheritanceSource& source, string& outText )
            {
                switch ( context._parentRule )
                {
                    case ResourceContext::ParentRule::KnownSchema:
                        return DataInheritance::flatten( layerText, layerID, context._pfnMakeSchema(), source, outText );
                    case ResourceContext::ParentRule::KnownType:
                        return DataInheritance::flatten( layerText, layerID, DataInheritanceSchema::makeFromType( *context._pTypeInfo ), source, outText );
                    case ResourceContext::ParentRule::KnownMerge:
                        return context._pfnMerge( layerText, layerID, source, outText );
                    case ResourceContext::ParentRule::CatalogTable:
                    {
                        XMLDocument doc;
                        if ( doc.parse( layerText, layerID ) == false || doc.getRoot().isValid() == false )
                        {
                            SW_LOG_ERROR_ID( "ResourceLayer.NotXML", "Resource layer '%#' could not be parsed: %#", layerID, doc.getLastError() );
                            return false;
                        }
                        const string_view     rootName = doc.getRoot().getName();
                        DataInheritanceSchema schema{ rootName };
                        bool                  bListed = false;
                        if ( DataCatalogInheritance::loadSchema( rootName, schema, bListed ) == false )
                            return false;
                        if ( bListed )
                            return DataCatalogInheritance::flatten( layerText, layerID, schema, source, outText );
                        if ( DataCatalogInheritance::loadDocumentRule( rootName, schema, bListed ) == false )
                            return false;
                        if ( bListed )
                            return DataInheritance::flatten( layerText, layerID, schema, source, outText );
                        SW_LOG_ERROR_ID( "ResourceLayer.NoRule", "Resource layer '%#': <%#> has no merge rule - add it to %# or read it through its own reader", layerID, rootName,
                                         DataCatalogInheritance::kConfigPath );
                        return false;
                    }
                    case ResourceContext::ParentRule::RootName:
                        break;
                }
                SW_LOG_ERROR_ID( "ResourceLayer.CookContext", "Resource layer '%#' reached the pack cook - layers are composed at load", layerID );
                return false;
            }

            /**
             * @brief 덮어쓰기 층 단계입니다. 층마다 `<뿌리>/<id>` 가 있으면 그 파일의 조건을 풀고 지금까지 합친 글을 암묵적 부모로 얹습니다.
             * @return 층 파일이 부모(`_base`)를 적었거나 · 합치지 못하면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool composeLayers( const ResourceContext& context, const ResourceTagSet& tags, string& inoutText, bool& outbLayered, vector<string>& inoutListLoadTag )
            {
                const string_view assetID = context._assetID.view();
                for ( const string& root : ResourceLayers::getLayers() )
                {
                    const AssetID layerID{ ResourceLayers::makeLayerAssetID( root, assetID ) };
                    if ( AssetIO::hasAsset( layerID ) == false )
                        continue;
                    string layerText;
                    if ( AssetIO::readTextAsset( layerID, layerText ) == false )
                    {
                        SW_LOG_ERROR_ID( "ResourceLayer.NotRead", "Resource layer '%#' could not be read", layerID.view() );
                        return false;
                    }
                    if ( DataInheritance::findBasePath( layerText ).empty() == false )
                    {
                        SW_LOG_ERROR_ID( "ResourceLayer.HasBase", "Resource layer '%#' writes %# - a layer's base is the resource it lies over ('%#')", layerID.view(), DataInheritance::kBaseAttribute, assetID );
                        return false;
                    }
                    // 층의 조건은 층의 차이에 얹는다 — 거짓이면 그 차이를 얹지 않는다.
                    string resolvedText;
                    bool   bResolved = false;
                    if ( ResourceComposer::resolveConditions( layerText, layerID.view(), ResourceTagTiming::Load, tags, resolvedText, bResolved, inoutListLoadTag ) == false )
                        return false;
                    XMLDocument layerDoc;
                    if ( layerDoc.parse( bResolved ? string_view( resolvedText ) : string_view( layerText ), layerID.view() ) == false || layerDoc.getRoot().isValid() == false )
                    {
                        SW_LOG_ERROR_ID( "ResourceLayer.NotXML2", "Resource layer '%#' could not be parsed: %#", layerID.view(), layerDoc.getLastError() );
                        return false;
                    }
                    layerDoc.getRoot().setAttribute( DataInheritance::kBaseAttribute, assetID );
                    ResourceLayerBaseSource source{ assetID, inoutText };
                    string                  mergedText;
                    if ( mergeLayer( layerDoc.saveToString(), layerID.view(), context, source, mergedText ) == false )
                    {
                        SW_LOG_ERROR_ID( "ResourceLayer.NotMerged", "Resource layer '%#' could not be laid over '%#' - the resource is not loaded", layerID.view(), assetID );
                        return false;
                    }
                    inoutText   = std::move( mergedText );
                    outbLayered = true;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ResourceContext ResourceContext::withAsset( const AssetID& assetID ) const
    {
        SW_ASSERT( _parentRule != ParentRule::RootName && "the pack cook reads files, not resources - layers are composed at load" );
        ResourceContext context = *this;
        context._assetID        = assetID;
        return context;
    }

    ResourceContext ResourceContext::forDocument()
    {
        return ResourceContext{ ParentRule::CatalogTable, nullptr, nullptr, nullptr, nullptr, AssetID{} };
    }

    ResourceContext ResourceContext::forSchema( DataInheritance::MakeSchemaFunction pfnMakeSchema )
    {
        return ResourceContext{ ParentRule::KnownSchema, pfnMakeSchema, nullptr, nullptr, nullptr, AssetID{} };
    }

    ResourceContext ResourceContext::forType( const TypeInfo& typeInfo )
    {
        return ResourceContext{ ParentRule::KnownType, nullptr, &typeInfo, nullptr, nullptr, AssetID{} };
    }

    ResourceContext ResourceContext::forMerge( DataInheritance::MergeFunction pfnMerge )
    {
        return ResourceContext{ ParentRule::KnownMerge, nullptr, nullptr, pfnMerge, nullptr, AssetID{} };
    }

    ResourceContext ResourceContext::forPackCook( IDataInheritanceSource& source )
    {
        return ResourceContext{ ParentRule::RootName, nullptr, nullptr, nullptr, &source, AssetID{} };
    }

    bool ResourceComposer::compose( string_view text, string_view id, const ResourceContext& context, string& outText, bool& outbComposed )
    {
        outbComposed = false;
        // 1. 부모
        string parentText;
        bool   bParents = false;
        if ( ResourceComposerInternal::composeParents( text, id, context, parentText, bParents ) == false )
            return false;
        string current = bParents ? std::move( parentText ) : string{};
        bool   bText   = bParents; // `current` 가 글을 들었는가(아니면 원문 `text` 그대로다)

        // 2. 조건 — 팩 쿠킹은 쿠킹 축만 풀고, 읽기는 모두 푼다(쿠킹 축 값은 런타임도 같다).
        const bool           bCook = context._parentRule == ResourceContext::ParentRule::RootName;
        const ResourceTagSet tags  = ResourceTags::capture();
        vector<string>       listLoadTag;
        string               conditionText;
        bool                 bConditions = false;
        if ( resolveConditions( bText ? string_view( current ) : text, id, bCook ? ResourceTagTiming::Cook : ResourceTagTiming::Load, tags, conditionText, bConditions, listLoadTag ) == false )
            return false;
        if ( bConditions )
        {
            current = std::move( conditionText );
            bText   = true;
        }

        // 3. 덮어쓰기 층 — 리소스 id 로 읽을 때만 얹는다.
        bool bLayered = false;
        if ( bCook == false && context._assetID.isEmpty() == false )
        {
            if ( bText == false )
                current = string{ text };
            if ( ResourceComposerInternal::composeLayers( context, tags, current, bLayered, listLoadTag ) == false )
                return false;
            bText = bText || bLayered;
            ResourceComposition::record( context._assetID.view(), listLoadTag, bLayered, bText ? string_view( current ) : text );
        }
        if ( bText )
        {
            outText      = std::move( current );
            outbComposed = true;
        }
        return true;
    }

    bool ResourceComposer::resolveConditions( string_view text, string_view id, ResourceTagTiming resolvedTiming, const ResourceTagSet& tags, string& outText, bool& outbChanged,
                                              vector<string>& inoutListLoadTag )
    {
        using Internal = ResourceConditionStageInternal;
        outbChanged    = false;
        // 조건을 적지 않은 파일(거의 전부)은 글 검색 한 번으로 끝난다 — 런타임 읽기마다 지나는 자리다.
        if ( text.find( DataInheritance::kWhenElement ) == string_view::npos || Internal::looksLikeXML( text ) == false )
            return true;
        XMLDocument doc;
        if ( doc.parse( text, id ) == false )
        {
            SW_LOG_ERROR_ID( "ResourceCondition.NotXML", "'%#' could not be parsed: %#", id, doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot();
        if ( root.isValid() == false )
            return true;
        Internal::Decision rootDecision;
        bool               bChanged = false;
        if ( Internal::decide( root, id, resolvedTiming, tags, rootDecision, bChanged, inoutListLoadTag ) == false )
            return false;
        if ( rootDecision._bRemoved == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "ResourceCondition.RootRemoved", "'%#': a condition removes the root <%#> - a resource cannot remove itself", id, root.getName() );
            return false;
        }
        XMLDocument outDoc;
        if ( Internal::writeResolved( root, rootDecision, outDoc.appendRoot( root.getName() ), id, resolvedTiming, tags, bChanged, inoutListLoadTag ) == false )
            return false;
        if ( bChanged == false )
            return true;
        outText     = outDoc.saveToString();
        outbChanged = true;
        return true;
    }
} // namespace sw
