#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceComposer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
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

        /** @brief 저장할 글에 원본의 조건을 되돌리는 일입니다(`ResourceComposer::restoreConditions`). */
        struct ResourceConditionRestoreInternal
        {
            using Stage = ResourceConditionStageInternal;

            /** @brief 원소에 적힌 조건 하나입니다. */
            struct When
            {
                XMLNode _node;
                uint8   _bTrue   : 1;
                uint8   _bRemove : 1;

                When()
                    : _node{}
                    , _bTrue{ SW_FALSE }
                    , _bRemove{ SW_FALSE }
                {
                }
            };

            [[nodiscard]] static bool isConditionName( string_view name ) { return name == DataInheritance::kIfAttribute || name == DataInheritance::kOpAttribute; }

            /** @brief @p element 의 조건을 지금 태그로 풀어 적힌 차례로 모읍니다. 틀린 조건은 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readWhens( const XMLNode& element, string_view id, const ResourceTagSet& tags, vector<When>& outListWhen )
            {
                for ( XMLNode node = element.findChild( DataInheritance::kWhenElement ); node.isValid(); node = node.findNextSibling( DataInheritance::kWhenElement ) )
                {
                    const utf8* pTest = node.findAttribute( DataInheritance::kIfAttribute );
                    if ( pTest == nullptr )
                    {
                        SW_LOG_ERROR_ID( "ResourceCondition.NoTest", "'%#': <%#> in <%#> has no %#", id, DataInheritance::kWhenElement, element.getName(), DataInheritance::kIfAttribute );
                        return false;
                    }
                    ResourceTagTest test;
                    if ( ResourceTags::compileTest( pTest, id, test ) == false )
                        return false;
                    When& when    = outListWhen.emplace_back();
                    when._node    = node;
                    when._bTrue   = test.evaluate( tags ) ? SW_TRUE : SW_FALSE;
                    when._bRemove = node.getAttributeText( DataInheritance::kOpAttribute ) == DataInheritance::kOpRemove ? SW_TRUE : SW_FALSE;
                }
                return true;
            }

            /** @brief 칸 @p name 을 정한 조건(참인 것 중 마지막)의 차례입니다. 없으면 비어 있습니다. */
            [[nodiscard]] static optional<uint32> findWinner( const vector<When>& listWhen, string_view name )
            {
                for ( size_t index = listWhen.size(); index > 0; --index )
                {
                    const When& when = listWhen[index - 1];
                    if ( when._bTrue == SW_TRUE && when._node.findAttribute( string( name ).c_str() ) != nullptr )
                        return static_cast<uint32>( index - 1 );
                }
                return nullopt;
            }

            /** @brief 아래 어디든 조건이 있으면 true 입니다. */
            [[nodiscard]] static bool hasConditions( const XMLNode& element )
            {
                for ( XMLNode child = element.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( Stage::isWhen( child ) || ( Stage::isElement( child ) && hasConditions( child ) ) )
                        return true;
                }
                return false;
            }

            /** @brief 같은 값의 속성 수입니다. */
            [[nodiscard]] static uint32 countSameAttributes( const XMLNode& left, const XMLNode& right )
            {
                uint32 count = 0;
                for ( XMLAttribute attribute = left.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    const utf8* pOther = right.findAttribute( attribute.getName() );
                    if ( pOther != nullptr && string_view( pOther ) == string_view( attribute.getValue() ) )
                        ++count;
                }
                return count;
            }

            /** @brief 원본 자식 하나가 사본에서 어떻게 되었는가입니다. */
            struct SourceChild
            {
                XMLNode _node;
                int32   _targetIndex;  ///< 짝인 사본 자식의 차례. 음수면 짝이 없다
                uint8   _bRemoved : 1; ///< 참인 조건이 뺐다(사본에 없다)
                uint8   _bWritten : 1; ///< 결과에 썼다

                SourceChild()
                    : _node{}
                    , _targetIndex{ -1 }
                    , _bRemoved{ SW_FALSE }
                    , _bWritten{ SW_FALSE }
                {
                }
            };

            /** @brief 원본 자식을 사본 자식과 짝짓습니다. 같은 이름 안에서 같은 값의 속성이 가장 많은 것이 짝이고, 그 이름이 양쪽에 하나뿐이면 값이 모두 달라도 짝이다. */
            [[nodiscard]] static bool pairChildren( const vector<XMLNode>& listTarget, vector<SourceChild>& inoutListSource, string_view id )
            {
                vector<uint8> listUsed( listTarget.size(), SW_FALSE );
                const auto    countNamed = []( auto&& getNode, size_t count, string_view name )
                {
                    uint32 total = 0;
                    for ( size_t index = 0; index < count; ++index )
                    {
                        total += string_view( getNode( index ).getName() ) == name ? 1u : 0u;
                    }
                    return total;
                };
                const auto getTarget = [&listTarget]( size_t index ) -> const XMLNode&
                { return listTarget[index]; };
                const auto getSource = [&inoutListSource]( size_t index ) -> const XMLNode&
                { return inoutListSource[index]._node; };
                for ( SourceChild& source : inoutListSource )
                {
                    if ( source._bRemoved == SW_TRUE )
                        continue;
                    const string_view name      = source._node.getName();
                    int32             bestIndex = -1;
                    uint32            bestScore = 0;
                    uint8             bTied     = SW_FALSE;
                    const bool        bOnlyPair = countNamed( getTarget, listTarget.size(), name ) == 1u && countNamed( getSource, inoutListSource.size(), name ) == 1u;
                    const bool        bNoValues = source._node.getFirstAttribute().isValid() == false;
                    for ( size_t index = 0; index < listTarget.size(); ++index )
                    {
                        if ( listUsed[index] == SW_TRUE || string_view( listTarget[index].getName() ) != name )
                            continue;
                        const uint32 score = countSameAttributes( source._node, listTarget[index] );
                        if ( bestIndex < 0 ? ( score > 0u || bOnlyPair || bNoValues ) : score > bestScore )
                        {
                            bestIndex = static_cast<int32>( index );
                            bestScore = score;
                            bTied     = SW_FALSE;
                        }
                        else if ( bestIndex >= 0 && score == bestScore )
                        {
                            bTied = SW_TRUE;
                        }
                    }
                    // 조건이 엉뚱한 원소에 붙으면 데이터가 조용히 틀어진다 — 짐작하지 않고 저장을 거절한다.
                    if ( bTied == SW_TRUE && hasConditions( source._node ) )
                    {
                        SW_LOG_ERROR_ID( "ResourceCondition.SaveAmbiguousElement", "'%#': <%#> holds a condition and matches several saved elements equally - not saved. Edit the conditions in the file", id, name );
                        return false;
                    }
                    if ( bestIndex >= 0 )
                    {
                        source._targetIndex                        = bestIndex;
                        listUsed[static_cast<size_t>( bestIndex )] = SW_TRUE;
                    }
                }
                return true;
            }

            /** @brief 짝이 없는 조건 든 원본 원소를 남기는가입니다. 목록 원소(같은 이름이 또 있거나 `item` · `_list…` 아래)는 지운 것이고, 차이 저장은 부모와 같아 빠진 것일 수 있다. */
            [[nodiscard]] static bool keepsUnpaired( const XMLNode& source, const XMLNode& sourceParent, const vector<XMLNode>& listTarget, bool bDelta )
            {
                if ( hasConditions( source ) == false )
                    return false;
                if ( bDelta )
                    return true;
                const string_view name       = source.getName();
                const string_view parentName = sourceParent.getName();
                uint32            sameNamed  = 0;
                for ( XMLNode other = sourceParent.findChild( source.getName() ); other.isValid(); other = other.findNextSibling( source.getName() ) )
                {
                    ++sameNamed;
                }
                const bool bTargetHasSame = VectorUtil::containsIf( listTarget, [name]( const XMLNode& target )
                { return string_view( target.getName() ) == name; } );
                return sameNamed <= 1u && bTargetHasSame == false && StringUtil::startsWith( parentName, "_list" ) == false;
            }

            /** @brief 원본 @p source 의 조건을 사본 @p target 에 되돌려 @p out 에 씁니다. */
            [[nodiscard]] static bool writeRestored( const XMLNode& source, const XMLNode& target, const XMLNode& out, string_view id, const ResourceTagSet& tags, bool bDelta )
            {
                vector<When> listWhen;
                if ( readWhens( source, id, tags, listWhen ) == false )
                    return false;
                // 1. 칸: 참인 조건이 정한 칸은 원본의 값으로 되돌린다.
                for ( XMLAttribute attribute = target.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( findWinner( listWhen, attribute.getName() ).has_value() == false )
                        out.setAttribute( attribute.getName(), attribute.getValue() );
                }
                for ( XMLAttribute attribute = source.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( findWinner( listWhen, attribute.getName() ).has_value() )
                        out.setAttribute( attribute.getName(), attribute.getValue() );
                }
                if ( StringUtil::isNullOrEmpty( target.getText() ) == false )
                    out.setValue( target.getText() );
                // 2. 조건: 그대로 남기되, 참인 조건이 정한 칸을 편집했으면 그 값이 조건의 값이다.
                for ( size_t index = 0; index < listWhen.size(); ++index )
                {
                    const XMLNode clone = out.appendClone( listWhen[index]._node );
                    for ( XMLAttribute attribute = listWhen[index]._node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                    {
                        if ( isConditionName( attribute.getName() ) || findWinner( listWhen, attribute.getName() ) != static_cast<uint32>( index ) )
                            continue;
                        const utf8* pEdited = target.findAttribute( attribute.getName() );
                        if ( pEdited != nullptr )
                            clone.setAttribute( attribute.getName(), pEdited );
                    }
                }
                // 3. 자식: 사본의 차례를 따르고, 사본에 없는 원본 원소는 원본에서 앞선 짝 뒤에 둔다.
                vector<XMLNode> listTarget;
                for ( XMLNode child = target.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( Stage::isElement( child ) )
                        listTarget.push_back( child );
                }
                vector<SourceChild> listSource;
                for ( XMLNode child = source.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( Stage::isElement( child ) == false || Stage::isWhen( child ) )
                        continue;
                    vector<When> listChildWhen;
                    if ( readWhens( child, id, tags, listChildWhen ) == false )
                        return false;
                    SourceChild& entry = listSource.emplace_back();
                    entry._node        = child;
                    entry._bRemoved    = VectorUtil::containsIf( listChildWhen, []( const When& when )
                       { return when._bTrue == SW_TRUE && when._bRemove == SW_TRUE; } )
                                           ? SW_TRUE
                                           : SW_FALSE;
                }
                if ( pairChildren( listTarget, listSource, id ) == false )
                    return false;
                const auto writeUnpairedBefore = [&]( size_t endIndex )
                {
                    for ( size_t index = 0; index < endIndex; ++index )
                    {
                        SourceChild& entry = listSource[index];
                        if ( entry._bWritten == SW_TRUE || entry._targetIndex >= 0 )
                            continue;
                        entry._bWritten = SW_TRUE;
                        if ( entry._bRemoved == SW_TRUE || keepsUnpaired( entry._node, source, listTarget, bDelta ) )
                            (void)out.appendClone( entry._node );
                    }
                };
                for ( size_t targetIndex = 0; targetIndex < listTarget.size(); ++targetIndex )
                {
                    const XMLNode& child       = listTarget[targetIndex];
                    size_t         sourceIndex = listSource.size();
                    for ( size_t index = 0; index < listSource.size(); ++index )
                    {
                        if ( listSource[index]._targetIndex == static_cast<int32>( targetIndex ) )
                            sourceIndex = index;
                    }
                    if ( sourceIndex == listSource.size() )
                    {
                        (void)out.appendClone( child ); // 편집이 더한 원소
                        continue;
                    }
                    writeUnpairedBefore( sourceIndex );
                    if ( writeRestored( listSource[sourceIndex]._node, child, out.appendChild( child.getName() ), id, tags, bDelta ) == false )
                        return false;
                }
                writeUnpairedBefore( listSource.size() );
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
                SW_LOG_ERROR_ID( "DataInheritance.NoSchema", "'%#': <%#> has %# but no merge rule the cook can see - add a <Document> row to %# (the cook does not read reflection)", id, rootName,
                                 DataInheritance::kBaseAttribute, DataCatalogInheritance::kTableAssetID );
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
                            SW_LOG_ERROR_ID( "DataInheritance.NoSchema", "'%#': <%#> has %# but no <Document> row in %# - the cook could not flatten it either", id, pRootName, DataInheritance::kBaseAttribute,
                                             DataCatalogInheritance::kTableAssetID );
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
             * @details 리플렉션 형식은 리플렉션에서 규칙을 만든다(코드가 곁에 있으니 표를 읽지 않는다). 표와 리플렉션이 같은지는
             *          `ResourceComposerTest.DocumentTableMatchesReflection` 이 본다. 카탈로그 표 문맥(`forDocument`)은 표를 읽는다 — 표는 엔진 팩에 담겨 배포 런타임의 층(DLC · 모드)도 읽는다.
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
                                         DataCatalogInheritance::kTableAssetID );
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
                for ( const string& root : engine::getSubsystem<ResourceComposition>().getLayers() )
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

    ResourceContext ResourceContext::withTagOverride( const ResourceTagSet& overrideTags ) const
    {
        SW_ASSERT( _parentRule != ParentRule::RootName && "the pack cook resolves cook axes only - a preview context never reaches it" );
        ResourceContext context = *this;
        context._pTagOverride   = overrideTags.getTags().empty() ? nullptr : &overrideTags;
        return context;
    }

    ResourceContext ResourceContext::forDocument()
    {
        return ResourceContext{ ParentRule::CatalogTable, nullptr, nullptr, nullptr, nullptr, AssetID{}, nullptr };
    }

    ResourceContext ResourceContext::forSchema( DataInheritance::MakeSchemaFunction pfnMakeSchema )
    {
        return ResourceContext{ ParentRule::KnownSchema, pfnMakeSchema, nullptr, nullptr, nullptr, AssetID{}, nullptr };
    }

    ResourceContext ResourceContext::forType( const TypeInfo& typeInfo )
    {
        return ResourceContext{ ParentRule::KnownType, nullptr, &typeInfo, nullptr, nullptr, AssetID{}, nullptr };
    }

    ResourceContext ResourceContext::forMerge( DataInheritance::MergeFunction pfnMerge )
    {
        return ResourceContext{ ParentRule::KnownMerge, nullptr, nullptr, pfnMerge, nullptr, AssetID{}, nullptr };
    }

    ResourceContext ResourceContext::forPackCook( IDataInheritanceSource& source )
    {
        return ResourceContext{ ParentRule::RootName, nullptr, nullptr, nullptr, &source, AssetID{}, nullptr };
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
        const ResourceTagSet tags  = context._pTagOverride != nullptr ? engine::getSubsystem<ResourceTags>().captureWith( *context._pTagOverride ) : engine::getSubsystem<ResourceTags>().capture();
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
            // 바꿔 끼운 태그로 합친 것은 기록하지 않는다 — 기록 · 접속 대조는 엔진 태그로 읽은 결과다(같은 id 의 엔진 합성을 덮지 않게).
            // shortcut: 바꿔 끼운 합성은 엔진 태그가 바뀌어도 다시 읽기 목록에 오르지 않는다 - 같은 id 를 엔진 태그로도 읽었으면 그 다시 읽기가 캐시의 변형까지 다시 읽는다(`UITextAssetCache::reload`). 변형만 든 리소스가 생기면 기록에 변형 칸을 둔다.
            if ( context._pTagOverride == nullptr )
                engine::getSubsystem<ResourceComposition>().record( context._assetID.view(), listLoadTag, bLayered, bText ? string_view( current ) : text );
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

    bool ResourceComposer::restoreConditions( string_view savedText, string_view sourceText, string_view id, const ResourceTagSet& tags, string& outText )
    {
        using Internal = ResourceConditionRestoreInternal;
        outText        = string{ savedText };
        if ( sourceText.find( DataInheritance::kWhenElement ) == string_view::npos || ResourceConditionStageInternal::looksLikeXML( sourceText ) == false )
            return true;
        XMLDocument sourceDoc;
        if ( sourceDoc.parse( sourceText, id ) == false || Internal::hasConditions( sourceDoc.getRoot() ) == false )
            return true; // 깨진 원본에는 지킬 조건이 없다 - 글자 `_when` 이 주석 · 값에 있었을 뿐이어도 여기로 온다
        XMLDocument savedDoc;
        if ( savedDoc.parse( savedText, id ) == false || savedDoc.getRoot().isValid() == false || string_view( savedDoc.getRoot().getName() ) != string_view( sourceDoc.getRoot().getName() ) )
        {
            SW_LOG_ERROR_ID( "ResourceCondition.SaveShapeChanged", "'%#' holds resource conditions but the text to save has another root - not saved, the conditions would be lost", id );
            return false;
        }
        XMLDocument   outDoc;
        const bool    bDelta = savedDoc.getRoot().getAttributeText( DataInheritance::kBaseAttribute ).empty() == false;
        const XMLNode root   = outDoc.appendRoot( savedDoc.getRoot().getName() );
        if ( Internal::writeRestored( sourceDoc.getRoot(), savedDoc.getRoot(), root, id, tags, bDelta ) == false )
            return false;
        outText = outDoc.saveToString();
        return true;
    }

    bool ResourceComposer::saveText( string_view absPath, string_view text, const ResourceTagSet* pTagOverride )
    {
        // 덮어쓰기 층을 얹어 읽은 사본을 원본 자리에 쓰면 층 값이 원본에 박힌다 - 고칠 자리는 층 파일이다.
        if ( engine::getSubsystem<ResourceComposition>().getLayers().empty() == false )
        {
            const AssetPaths* pPaths = engine::findSubsystem<AssetPaths>();
            const string      id     = pPaths != nullptr ? pPaths->toAssetID( absPath ) : string{};
            if ( id.empty() == false && engine::getSubsystem<ResourceComposition>().isLayered( id ) )
            {
                SW_LOG_ERROR_ID( "ResourceLayer.SaveWouldBake", "'%#' was read with a resource layer over it - not saved, the layer's values would be written into the original (edit the layer file instead)", id );
                return false;
            }
        }
        if ( FileUtil::ensureParentDirectoryExists( absPath ) == false )
            return false;
        string sourceText;
        if ( FileUtil::isRegularFile( absPath ) == false || FileUtil::readTextFile( absPath, sourceText ) == false )
            return FileUtil::writeTextFile( absPath, text );
        string               restoredText;
        const ResourceTags&  engineTags = engine::getSubsystem<ResourceTags>();
        const ResourceTagSet tags       = pTagOverride != nullptr ? engineTags.captureWith( *pTagOverride ) : engineTags.capture();
        if ( restoreConditions( text, sourceText, absPath, tags, restoredText ) == false )
            return false;
        return FileUtil::writeTextFile( absPath, restoredText );
    }

    expected<void> ResourceComposer::checkDeltaReadsBack( const string& deltaText, const string_view fullText, const string_view baseID, const ResourceRereadFunction& reread )
    {
        if ( reread.isBound() == false || string_view{ reread( deltaText ) } == fullText )
            return {};
        return unexpected( Error{ ErrorKind::Unsupported, "the difference from base does not read back the same (a value the base writes cannot be reset to the default by a variant)", baseID } );
    }

    expected<void> ResourceComposer::makeCheckedDelta( const string_view fullText, const string_view parentFullText, const DataInheritanceSchema& schema, const string_view baseID, const ResourceRereadFunction& reread,
                                                       string& outDeltaText )
    {
        if ( DataInheritance::makeDelta( fullText, parentFullText, schema, baseID, outDeltaText ) )
            return checkDeltaReadsBack( outDeltaText, fullText, baseID, reread );
        return unexpected( Error{ ErrorKind::InvalidArgument, "the difference from base could not be made (see the log)", baseID } );
    }

    bool ResourceComposer::saveDelta( const string_view absPath, const string_view fullText, const string_view parentFullText, const DataInheritanceSchema& schema, const string_view baseID, const ResourceRereadFunction& reread )
    {
        string deltaText;
        if ( const expected<void> delta = makeCheckedDelta( fullText, parentFullText, schema, baseID, reread, deltaText ); delta.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ResourceSave.DeltaNotSaved", "'%#': %# '%#' - not saved", string{ absPath }.c_str(), delta.error().message(), delta.error().detail() );
            return false;
        }
        if ( saveText( absPath, deltaText ) )
            return true;
        SW_LOG_ERROR_ID( "ResourceSave.WriteFailed", "'%#' could not be written", string{ absPath }.c_str() );
        return false;
    }
} // namespace sw
