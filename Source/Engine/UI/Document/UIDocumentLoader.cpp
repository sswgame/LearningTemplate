#include "pch.h"

#include "Engine/UI/Document/UIDocumentLoader.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/Widgets/UserWidget.h"

namespace sw
{
    namespace
    {
        struct UIDocumentLoaderInternal
        {
            static constexpr utf8   kScreenDescElement[]     = "UIScreenDef";
            static constexpr utf8   kStyleSheetListElement[] = "_listStyleSheet";
            static constexpr utf8   kAnimationListElement[]  = "_listAnimation";
            static constexpr utf8   kItemElement[]           = "item";
            static constexpr utf8   kFragmentProperty[]      = "_document";
            static constexpr uint32 kMaxFragmentDepth        = 16; ///< 조각 안의 조각 깊이 상한입니다(자기를 다시 부르는 연쇄는 그 전에 잡으므로 이것은 안전망입니다).

            /** @brief 파싱 한 번의 컨텍스트입니다. 원문(줄 번호), 경로, 결과, 오류를 가집니다. */
            struct ParseContext
            {
                string_view      _text;
                string_view      _path;
                UIDocumentAsset* _pAsset;

                uint32 findLine( const XMLNode& node ) const
                {
                    return XMLDocument::computeLineNumber( _text, node.getSourceOffset() );
                }

                /** @brief 읽기 오류입니다. 문장은 하나이고 대상이 `경로:줄: 까닭` 이다(대상 길이에서 잘린다). */
                unexpected<Error> fail( const XMLNode& node, string_view message ) const
                {
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the UI document is invalid", makeError( _path, findLine( node ), message ) } );
                }

                /** @brief 구조체나 컨테이너 필드 요소 @p source 를 @p destinationParent 아래로 옮겨 적고, 그 안의 바인딩 속성은 분리해 @p inoutNode 에 모읍니다. */
                void copyPropertyElement( const XMLNode& source, const XMLNode& destinationParent, const string& path,
                                          UIDocumentNode& inoutNode ) const
                {
                    const XMLNode destination = destinationParent.appendChild( source.getName() );
                    for ( XMLAttribute attribute = source.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                    {
                        if ( isBindingValue( attribute.getValue() ) )
                        {
                            inoutNode._listBinding.push_back(
                                UIBindingDef{ path + "." + attribute.getName(), string( attribute.getValue() ), kInvalidWidgetID, findLine( source ) } );
                            continue;
                        }
                        destination.appendAttribute( attribute.getName(), attribute.getValue() );
                    }
                    const utf8* pText = source.getText();
                    if ( StringUtil::isNullOrEmpty( pText ) == false )
                        destination.setValue( pText );
                    for ( XMLNode child = source.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        copyPropertyElement( child, destination, path + "." + child.getName(), inoutNode );
                    }
                }

                /** @brief 위젯 요소 @p element 를 노드로 파싱해 문서 끝에 붙이고(자식은 그 뒤에), 그 인덱스를 @p outIndex 에 적습니다. */
                [[nodiscard]] expected<void> parseWidget( const XMLNode& element, uint32& outIndex ) const
                {
                    const utf8*     pTag  = element.getName();
                    const TypeInfo* pType = findWidgetType( pTag );
                    if ( pType == nullptr )
                        return fail( element, string( "'" ) + pTag + "' is not a widget type" );

                    UIDocumentNode node{};
                    node._typeName   = pType->_name;
                    node._sourceLine = findLine( element );

                    // 위젯 PROPERTY 로 읽을 요소만 남긴 사본이다. 자식 위젯과 바인딩 식은 분리한다.
                    XMLDocument   propertyDocument;
                    const XMLNode propertyRoot = propertyDocument.appendRoot( pType->_name.c_str() );
                    for ( XMLAttribute attribute = element.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                    {
                        const utf8* pName = attribute.getName();
                        if ( isKnownProperty( *pType, pName ) == false )
                            return fail( element, string( "'" ) + pTag + "' has unknown attribute '" + pName + "'" );
                        if ( isBindingValue( attribute.getValue() ) )
                        {
                            node._listBinding.push_back( UIBindingDef{ string( pName ), string( attribute.getValue() ), kInvalidWidgetID, node._sourceLine } );
                            continue;
                        }
                        propertyRoot.appendAttribute( pName, attribute.getValue() );
                    }

                    vector<XMLNode> listChildWidget;
                    for ( XMLNode child = element.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        const utf8* pChildName = child.getName();
                        if ( findWidgetType( pChildName ) != nullptr )
                        {
                            listChildWidget.push_back( child );
                            continue;
                        }
                        if ( isKnownProperty( *pType, pChildName ) == false )
                            return fail( child, string( "'" ) + pTag + "' has unknown element <" + pChildName + ">" );
                        copyPropertyElement( child, propertyRoot, string( pChildName ), node );
                    }

                    if ( pType->isDerivedFrom( UserWidget::StaticType() ) )
                    {
                        if ( listChildWidget.empty() == false )
                            return fail( listChildWidget.front(), string( "'" ) + pTag + "' cannot have child widgets - its content is the _document" );
                        const utf8* pFragment = element.findAttribute( kFragmentProperty );
                        if ( StringUtil::isNullOrEmpty( pFragment ) || isBindingValue( pFragment ) )
                            return fail( element, string( "'" ) + pTag + "' needs a _document path" );
                        node._fragment               = normalizeDocumentPath( pFragment );
                        vector<string>& listFragment = _pAsset->_listFragment;
                        if ( VectorUtil::contains( listFragment, node._fragment ) == false )
                            listFragment.push_back( node._fragment );
                    }
                    else if ( listChildWidget.empty() == false && pType->isDerivedFrom( PanelWidget::StaticType() ) == false )
                        return fail( listChildWidget.front(), string( "'" ) + pTag + "' cannot have children - it is not a panel" );

                    node._propertyXML = propertyDocument.saveToString();
                    outIndex          = static_cast<uint32>( _pAsset->_listNode.size() );
                    _pAsset->_listNode.push_back( std::move( node ) );
                    for ( const XMLNode& child : listChildWidget )
                    {
                        uint32 childIndex{ 0 };
                        if ( expected<void> parsed = parseWidget( child, childIndex ); parsed.has_value() == false )
                            return parsed;
                        _pAsset->_listNode[outIndex]._listChildIndex.push_back( childIndex );
                    }
                    return {};
                }

                /** @brief 화면 서술 요소를 읽습니다. 모르는 속성이나 읽지 못한 값은 오류입니다. */
                [[nodiscard]] expected<void> parseScreenDesc( const XMLNode& element ) const
                {
                    vector<SchemaOrphanValue> listOrphan;
                    const SoftReadResult      result = XMLSerializer::deserializeSoft( &_pAsset->_screenDesc, *UIScreenDef::StaticType(), element.toString(), &listOrphan );
                    return acceptSoftRead( element, "UIScreenDef", result, listOrphan );
                }

                /** @brief 스타일 시트 목록(`<item>경로</item>`)을 읽습니다. */
                [[nodiscard]] expected<void> parseStyleSheetList( const XMLNode& element ) const
                {
                    for ( XMLNode item = element.findChild(); item.isValid(); item = item.findNextSibling() )
                    {
                        if ( StringUtil::equals( item.getName(), kItemElement ) == false )
                            return fail( item, string( "_listStyleSheet has unknown element <" ) + item.getName() + ">" );
                        const utf8* pPath = item.getText();
                        if ( StringUtil::isNullOrEmpty( pPath ) )
                            return fail( item, "_listStyleSheet has an empty item" );
                        _pAsset->_listStyleSheet.push_back( normalizeDocumentPath( pPath ) );
                    }
                    return {};
                }

                /** @brief 애니메이션 목록(`<UIAnimation>` 요소들)을 읽습니다. 모르는 필드, 읽지 못한 값, 이름 없음, 겹친 이름은 오류입니다. */
                [[nodiscard]] expected<void> parseAnimationList( const XMLNode& element ) const
                {
                    UIAnimationList           list{};
                    vector<SchemaOrphanValue> listOrphan;
                    const string              text   = string( "<UIAnimationList>" ) + element.toString() + "</UIAnimationList>";
                    const SoftReadResult      result = XMLSerializer::deserializeSoft( &list, *UIAnimationList::StaticType(), text, &listOrphan );
                    if ( expected<void> accepted = acceptSoftRead( element, "_listAnimation", result, listOrphan ); accepted.has_value() == false )
                        return accepted;
                    vector<UIAnimation>& listAnimation = _pAsset->_listAnimation;
                    for ( UIAnimation& animation : list._listAnimation )
                    {
                        if ( animation._name.empty() )
                            return fail( element, "_listAnimation has an animation without _name" );
                        for ( const UIAnimation& other : listAnimation )
                        {
                            if ( other._name == animation._name )
                                return fail( element, string( "_listAnimation has two animations named '" ) + animation._name.c_str() + "'" );
                        }
                        listAnimation.push_back( std::move( animation ) );
                    }
                    return {};
                }

                /** @brief 소프트 읽기에 문제가 없는지 봅니다. 있으면 모두 적은 실패입니다(`describeSoftReadProblem`). */
                [[nodiscard]] expected<void> acceptSoftRead( const XMLNode& element, const string_view what, const SoftReadResult result, const vector<SchemaOrphanValue>& listOrphan ) const
                {
                    const string problem = describeSoftReadProblem( what, result, listOrphan );
                    if ( problem.empty() )
                        return {};
                    return fail( element, problem );
                }
            };

            /** @brief 생성 한 번의 컨텍스트입니다. 조각 캐시, 지금 지나는 조각 연쇄(자기를 다시 부르는지 판정), 바인딩, 오류를 가집니다. */
            struct InstantiateContext
            {
                UIDocumentCache*      _pCache;
                const ResourceTagSet* _pTagOverride; ///< 조각을 찾을 변형의 태그(문서와 같은 것)
                vector<string>        _listFragmentStack;
                vector<UIBindingDef>* _pOutListBinding;
                string*               _pError;

                unique_ptr<Widget> failInstantiate( const UIDocumentAsset& document, const UIDocumentNode& node, string_view message )
                {
                    *_pError = makeError( document._path, node._sourceLine, message );
                    return {};
                }

                /** @brief 노드 @p nodeIndex 와 그 아래를 만듭니다. @p namePrefix 가 있으면(조각 안) 이름을 `접두.이름` 으로 감쌉니다. */
                unique_ptr<Widget> instantiateNode( const UIDocumentAsset& document, uint32 nodeIndex, const string& namePrefix )
                {
                    unique_ptr<Widget>    widget;
                    const UIDocumentNode& node               = document._listNode[nodeIndex];
                    const TypeInfo*       pType              = engine::getSubsystem<TypeRegistry>().findType( node._typeName );
                    const bool            bNotWidgetTypeName = pType == nullptr || pType->isAlive() == false || pType->isDerivedFrom( Widget::StaticType() ) == false;
                    if ( bNotWidgetTypeName )
                    {
                        widget = failInstantiate( document, node, string( "'" ) + node._typeName.c_str() + "' is not a widget type" );
                        return widget;
                    }

                    widget = UIDocumentLoader::createWidget( *pType );
                    if ( widget == nullptr )
                    {
                        widget = failInstantiate( document, node, string( "'" ) + node._typeName.c_str() + "' cannot be created (abstract or no default constructor)" );
                        return widget;
                    }
                    const TypeInfo* pDynamicType = widget->getTypeInfo();
                    if ( pDynamicType == nullptr || pDynamicType->_name != pType->_name )
                    {
                        widget = failInstantiate( document, node, string( "'" ) + node._typeName.c_str() + "' does not override getTypeInfo()" );
                        return widget;
                    }

                    vector<SchemaOrphanValue> listOrphan;
                    const SoftReadResult      result  = XMLSerializer::deserializeSoft( widget.get(), *pType, node._propertyXML, &listOrphan );
                    const string              problem = describeSoftReadProblem( string( "'" ) + node._typeName.c_str() + "'", result, listOrphan );
                    if ( problem.empty() == false )
                    {
                        widget = failInstantiate( document, node, problem );
                        return widget;
                    }

                    if ( namePrefix.empty() == false && widget->getName().empty() == false )
                        widget->setName( hashed_string( namePrefix + "." + widget->getName().c_str() ) );
                    for ( const UIBindingDef& binding : node._listBinding )
                    {
                        UIBindingDef instanceBinding = binding;
                        instanceBinding._widget      = widget->getID();
                        _pOutListBinding->push_back( std::move( instanceBinding ) );
                    }

                    PanelWidget* pPanel = castTo<PanelWidget>( widget.get() );
                    if ( node._fragment.empty() == false )
                    {
                        if ( pPanel == nullptr )
                        {
                            widget = failInstantiate( document, node, "a fragment needs a panel to hold it" );
                            return widget;
                        }
                        vector<string>& listStack  = _listFragmentStack;
                        const bool      bRecursive = VectorUtil::contains( listStack, node._fragment );
                        const bool      bTooDeep   = listStack.size() >= kMaxFragmentDepth;
                        if ( bRecursive || bTooDeep )
                        {
                            string chain;
                            for ( const string& path : listStack )
                            {
                                chain += path + " -> ";
                            }
                            chain += node._fragment;
                            widget = failInstantiate( document, node, ( bRecursive ? "fragment includes itself: " : "fragments nest too deep: " ) + chain );
                            return widget;
                        }
                        const expected<shared_ptr<const UIDocumentAsset>> loaded = _pCache->findOrLoad( node._fragment, *_pTagOverride );
                        if ( loaded.has_value() == false )
                        {
                            widget = failInstantiate( document, node, "fragment '" + node._fragment + "' is not loaded: " + string( loaded.error().message().data(), loaded.error().message().size() ) + ": " + string( loaded.error().detail().data(), loaded.error().detail().size() ) );
                            return widget;
                        }
                        const shared_ptr<const UIDocumentAsset>& fragment = *loaded;
                        // 조각 안의 이름은 이 위젯의 (감싼) 이름으로 감싼다. 이름 없는 조각은 바깥 접두를 그대로 넘긴다.
                        string childPrefix = namePrefix;
                        if ( widget->getName().empty() == false )
                            childPrefix = widget->getName().c_str();
                        listStack.push_back( node._fragment );
                        unique_ptr<Widget> child = instantiateNode( *fragment, 0, childPrefix );
                        listStack.pop_back();
                        if ( child == nullptr )
                        {
                            widget.reset();
                            return widget;
                        }
                        pPanel->addChild( std::move( child ) );
                        return widget;
                    }

                    for ( const uint32 childIndex : node._listChildIndex )
                    {
                        unique_ptr<Widget> child = instantiateNode( document, childIndex, namePrefix );
                        if ( child == nullptr )
                        {
                            widget.reset();
                            return widget;
                        }
                        pPanel->addChild( std::move( child ) );
                    }
                    return widget;
                }
            };

            /** @brief `<경로>:<줄>: <이유>` 형식의 문구입니다. 줄을 모르면(0) 경로만 적습니다. */
            static string makeError( string_view path, uint32 line, string_view message )
            {
                string error( path );
                if ( line > 0 )
                {
                    StringBuilder<constant::buffer::kMax256> errorPart;
                    errorPart.appendFormat( ":%#", line );
                    error += errorPart.c_str();
                }
                error += ": ";
                error += message;
                return error;
            }

            /** @brief 속성 값이 바인딩 식(`{` 로 시작)인지 판정합니다. 식은 값으로 읽지 않고 분리합니다. */
            static bool isBindingValue( const utf8* pValue ) { return pValue != nullptr && pValue[0] == '{'; }

            /** @brief 요소 이름 @p pName 으로 만들 수 있는 위젯 타입이면 그 타입을 반환합니다. 파일의 이름을 전역 이름 테이블에 넣지 않고 찾기만 합니다. */
            static const TypeInfo* findWidgetType( const utf8* pName )
            {
                if ( StringUtil::isNullOrEmpty( pName ) )
                    return nullptr;
                const hashed_string name = hashed_string::findInterned( string_view( pName ) );
                if ( name.empty() )
                    return nullptr;
                const TypeInfo* pType          = engine::getSubsystem<TypeRegistry>().findType( name );
                const bool      bNotWidgetType = pType == nullptr || pType->isAlive() == false || pType->isDerivedFrom( Widget::StaticType() ) == false;
                if ( bNotWidgetType )
                    return nullptr;
                return pType;
            }

            /** @brief @p pName 이 타입(상속 포함)의 프로퍼티 이름이나 별칭인지 판정합니다(철자 그대로이며, 직렬화의 이름 맞춤과 같습니다). */
            static bool isKnownProperty( const TypeInfo& type, const utf8* pName )
            {
                if ( StringUtil::isNullOrEmpty( pName ) )
                    return false;
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( prop.matchesNameText( pName ) )
                        return true;
                }
                return false;
            }

            /** @brief 경로를 정규화해 적습니다(캐시 키와 조각 연쇄 비교용). */
            static string normalizeDocumentPath( string_view path ) { return FileUtil::normalizePath( path ); }

            /** @brief orphan 하나를 `이름 = '텍스트'` 로 적습니다(안쪽 요소면 타입 이름이 앞에 붙습니다). */
            static string describeOrphan( const SchemaOrphanValue& orphan )
            {
                string description = orphan._writtenName.empty() ? string( orphan._name.c_str() ) : orphan._writtenName;
                if ( orphan._text.empty() == false )
                    description += "' = '" + orphan._text;
                return description;
            }

            /**
             * @brief 소프트 읽기 결과에서 문제를 모두 한 줄로 적습니다. 문제가 없으면 빈 글입니다.
             * @details 첫 문제에서 멈추지 않는다 — 범위 밖 값 하나가 같은 요소의 모르는 이름 · 읽지 못한 값을 삼키지 않게(C-60).
             */
            static string describeSoftReadProblem( const string_view what, const SoftReadResult result, const vector<SchemaOrphanValue>& listOrphan )
            {
                if ( result == SoftReadResult::NotParsed )
                    return string( what ) + " cannot be read";
                string problem;
                for ( const SchemaOrphanValue& orphan : listOrphan )
                {
                    problem += problem.empty() ? string( what ) + " has unknown or unreadable '" : string( ", '" );
                    problem += describeOrphan( orphan ) + "'";
                }
                // 읽지 못한 칸 · 범위 밖 값은 읽기 공통 끝이 칸마다 로그로 알렸다.
                if ( result == SoftReadResult::Rejected )
                    problem += ( problem.empty() ? string( what ) + " has" : string( "; and" ) ) + " a field that cannot be read or is outside its PROPERTY( Min, Max )";
                return problem;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<void> UIDocumentLoader::parse( string_view sourceText, string_view path, const ResourceContext& composeContext, UIDocumentAsset& outAsset )
    {
        using Internal = UIDocumentLoaderInternal;
        UIDocumentAsset asset{};
        asset._path = Internal::normalizeDocumentPath( path );

        // 리소스 글의 한 길(조건 · 층)을 지난다 — 예전에는 원문을 바로 읽어 `_when="screen.compact"` 가 풀리지 않았다(`docs/PlatformTargets.md`).
        string flatText;
        bool   bComposed = false;
        if ( ResourceComposer::compose( sourceText, path, composeContext, flatText, bComposed ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the UI document could not be composed (the log says why)", path } );
        const string_view text = bComposed ? string_view( flatText ) : sourceText;

        XMLDocument document;
        if ( document.parse( text, path ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the UI document is not well-formed XML", document.getLastError() } );
        const Internal::ParseContext context{ text, path, &asset };
        const XMLNode                root = document.getRoot();
        if ( root.isValid() == false || StringUtil::equals( root.getName(), UIDocumentAsset::kRootElementName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the UI document is invalid", Internal::makeError( path, 1, "the root element must be <UIDocument>" ) } );

        // 루트 속성은 버전 번호 하나뿐이다. 이전 형식 리더는 없다.
        bool bVersioned{ false };
        for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( StringUtil::equals( attribute.getName(), sw::kSchemaVersionKey ) == false )
                return context.fail( root, string( "UIDocument has unknown attribute '" ) + attribute.getName() + "'" );
            int32 version{ 0 };
            if ( StringUtil::parseInt( string( attribute.getValue() ), version ) == false || version != static_cast<int32>( UIDocumentAsset::kVersion ) )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#%#' is not supported (expected %#)", string( "_schemaVersion '" ), attribute.getValue(), UIDocumentAsset::kVersion );
                return context.fail( root, textBuilder.view() );
            }
            bVersioned = true;
        }
        if ( bVersioned == false )
            return context.fail( root, "UIDocument needs _schemaVersion" );

        bool bScreenDesc{ false };
        bool bRootWidget{ false };
        for ( XMLNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, Internal::kScreenDescElement ) )
            {
                if ( bScreenDesc )
                    return context.fail( child, "UIDocument has more than one UIScreenDef" );
                bScreenDesc = true;
                if ( expected<void> parsed = context.parseScreenDesc( child ); parsed.has_value() == false )
                    return parsed;
                continue;
            }
            if ( StringUtil::equals( pName, Internal::kStyleSheetListElement ) )
            {
                if ( expected<void> parsed = context.parseStyleSheetList( child ); parsed.has_value() == false )
                    return parsed;
                continue;
            }
            if ( StringUtil::equals( pName, Internal::kAnimationListElement ) )
            {
                if ( expected<void> parsed = context.parseAnimationList( child ); parsed.has_value() == false )
                    return parsed;
                continue;
            }
            if ( Internal::findWidgetType( pName ) == nullptr )
                return context.fail( child, string( "UIDocument has unknown element <" ) + pName + "> (not a widget type)" );
            if ( bRootWidget )
                return context.fail( child, "UIDocument has more than one root widget" );
            bRootWidget = true;
            uint32 rootIndex{ 0 };
            if ( expected<void> parsed = context.parseWidget( child, rootIndex ); parsed.has_value() == false )
                return parsed;
        }
        if ( bRootWidget == false )
            return context.fail( root, "UIDocument has no root widget" );

        outAsset = std::move( asset );
        return {};
    }

    unique_ptr<Widget> UIDocumentLoader::createWidget( const TypeInfo& type )
    {
        // `Widget` 이 첫 기반(단일 상속 연쇄)이므로 블록 시작이 곧 `Widget*` 이다.
        static const hashed_string s_ctor( "$ctor" );
        const bool                 bNotConstructibleWidget = type.canConstruct() == false || type._size == 0 || type.isDerivedFrom( Widget::StaticType() ) == false;
        if ( bNotConstructibleWidget )
            return {};
        const FunctionInfo* pCtor = type.findMethod( s_ctor );
        if ( pCtor == nullptr || pCtor->_invoker.isBound() == false )
            return {};
        // 블록은 생성자를 부르는 동안만 `MemoryBlock` 이 쥐고, 다 만든 위젯은 `unique_ptr<Widget>` 이 소유한다(가상 소멸자 · `Memory::free` 짝).
        MemoryBlock block = MemoryBlock::allocate( type._size );
        if ( block.get() == nullptr )
            return {};
        (void)pCtor->_invoker( block.get(), TaskArgs{} );
        return unique_ptr<Widget>( static_cast<Widget*>( block.release() ) );
    }

    expected<unique_ptr<Widget>> UIDocumentLoader::instantiate( const UIDocumentAsset& document, UIDocumentCache& cache, const ResourceTagSet& tagOverride, vector<UIBindingDef>& outListBinding )
    {
        if ( document._listNode.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the UI document cannot be built", UIDocumentLoaderInternal::makeError( document._path, 0, "document has no root widget" ) } );
        const size_t                                 bindingStart = outListBinding.size();
        string                                       reason; // 위젯을 만드는 깊은 곳(조각 · 필드 읽기)이 `경로:줄: 까닭` 을 적는다
        UIDocumentLoaderInternal::InstantiateContext context{ &cache, &tagOverride, vector<string>{ document._path }, &outListBinding, &reason };
        unique_ptr<Widget>                           root = context.instantiateNode( document, 0, string{} );
        if ( root == nullptr )
        {
            outListBinding.resize( bindingStart );
            return unexpected( Error{ ErrorKind::InvalidArgument, "the UI document cannot be built", reason } );
        }
        return root;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( UIDocumentAsset, UIDocumentAsset::kAssetType );
} // namespace sw
