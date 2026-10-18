#include "pch.h"

#include "Engine/UI/Document/UIDocumentLoader.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Widgets/UserWidget.h"

namespace sw
{
    namespace
    {
        struct UIDocumentLoaderInternal
        {
            static constexpr utf8   kScreenDescElement[]     = "UIScreenDesc";
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
                string*          _pError;
            };

            /** @brief 생성 한 번의 컨텍스트입니다. 조각 캐시, 지금 지나는 조각 연쇄(자기를 다시 부르는지 판정), 바인딩, 오류를 가집니다. */
            struct InstantiateContext
            {
                UIDocumentCache*       _pCache;
                vector<string>         _listFragmentStack;
                vector<UIBindingDesc>* _pOutListBinding;
                string*                _pError;
            };

            /** @brief `<경로>:<줄>: <이유>` 형식의 문구입니다. 줄을 모르면(0) 경로만 적습니다. */
            static string makeError( string_view path, uint32 line, string_view message )
            {
                string error( path );
                if ( line > 0 )
                    error += ":" + to_string( line );
                error += ": ";
                error += message;
                return error;
            }

            static uint32 findLine( const ParseContext& context, const XMLNode& node ) { return XMLDocument::computeLineNumber( context._text, node.getSourceOffset() ); }

            static bool fail( const ParseContext& context, const XMLNode& node, string_view message )
            {
                *context._pError = makeError( context._path, findLine( context, node ), message );
                return false;
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

            /** @brief @p pName 이 타입(상속 포함)의 프로퍼티 이름이나 별칭인지 판정합니다(대소문자 무시이며, 직렬화의 이름 맞춤과 같습니다). */
            static bool isKnownProperty( const TypeInfo& type, const utf8* pName )
            {
                if ( StringUtil::isNullOrEmpty( pName ) )
                    return false;
                const uint32 nameHash = static_cast<uint32>( hashed_string::computeHash( string_view( pName ) ) );
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( prop.matchesNameHash( nameHash ) )
                        return true;
                }
                return false;
            }

            /** @brief 구조체나 컨테이너 필드 요소 @p source 를 @p destinationParent 아래로 옮겨 적고, 그 안의 바인딩 속성은 분리해 @p inoutNode 에 모읍니다. */
            static void copyPropertyElement( const ParseContext& context, const XMLNode& source, const XMLNode& destinationParent, const string& path,
                                             UIDocumentNode& inoutNode )
            {
                const XMLNode destination = destinationParent.appendChild( source.getName() );
                for ( XMLAttribute attribute = source.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    if ( isBindingValue( attribute.getValue() ) )
                    {
                        inoutNode._listBinding.push_back(
                            UIBindingDesc{ path + "." + attribute.getName(), string( attribute.getValue() ), kInvalidWidgetID, findLine( context, source ) } );
                        continue;
                    }
                    destination.appendAttribute( attribute.getName(), attribute.getValue() );
                }
                const utf8* pText = source.getText();
                if ( StringUtil::isNullOrEmpty( pText ) == false )
                    destination.setValue( pText );
                for ( XMLNode child = source.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    copyPropertyElement( context, child, destination, path + "." + child.getName(), inoutNode );
                }
            }

            /** @brief 경로를 정규화해 적습니다(캐시 키와 조각 연쇄 비교용). */
            static string normalizeDocumentPath( string_view path ) { return FileUtil::normalizePath( path ); }

            /** @brief 위젯 요소 @p element 를 노드로 파싱해 문서 끝에 붙이고(자식은 그 뒤에), 그 인덱스를 @p outIndex 에 적습니다. */
            [[nodiscard]] static bool parseWidget( const ParseContext& context, const XMLNode& element, uint32& outIndex )
            {
                const utf8*     pTag  = element.getName();
                const TypeInfo* pType = findWidgetType( pTag );
                if ( pType == nullptr )
                    return fail( context, element, string( "'" ) + pTag + "' is not a widget type" );

                UIDocumentNode node{};
                node._typeName   = pType->_name;
                node._sourceLine = findLine( context, element );

                // 위젯 PROPERTY 로 읽을 요소만 남긴 사본이다. 자식 위젯과 바인딩 식은 분리한다.
                XMLDocument   propertyDocument;
                const XMLNode propertyRoot = propertyDocument.appendRoot( pType->_name.c_str() );
                for ( XMLAttribute attribute = element.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const utf8* pName = attribute.getName();
                    if ( isKnownProperty( *pType, pName ) == false )
                        return fail( context, element, string( "'" ) + pTag + "' has unknown attribute '" + pName + "'" );
                    if ( isBindingValue( attribute.getValue() ) )
                    {
                        node._listBinding.push_back( UIBindingDesc{ string( pName ), string( attribute.getValue() ), kInvalidWidgetID, node._sourceLine } );
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
                        return fail( context, child, string( "'" ) + pTag + "' has unknown element <" + pChildName + ">" );
                    copyPropertyElement( context, child, propertyRoot, string( pChildName ), node );
                }

                if ( pType->isDerivedFrom( UserWidget::StaticType() ) )
                {
                    if ( listChildWidget.empty() == false )
                        return fail( context, listChildWidget.front(), string( "'" ) + pTag + "' cannot have child widgets - its content is the _document" );
                    const utf8* pFragment = element.findAttribute( kFragmentProperty );
                    if ( StringUtil::isNullOrEmpty( pFragment ) || isBindingValue( pFragment ) )
                        return fail( context, element, string( "'" ) + pTag + "' needs a _document path" );
                    node._fragment               = normalizeDocumentPath( pFragment );
                    vector<string>& listFragment = context._pAsset->_listFragment;
                    if ( std::find( listFragment.begin(), listFragment.end(), node._fragment ) == listFragment.end() )
                        listFragment.push_back( node._fragment );
                }
                else if ( listChildWidget.empty() == false && pType->isDerivedFrom( PanelWidget::StaticType() ) == false )
                    return fail( context, listChildWidget.front(), string( "'" ) + pTag + "' cannot have children - it is not a panel" );

                node._propertyXML = propertyDocument.saveToString();
                outIndex          = static_cast<uint32>( context._pAsset->_listNode.size() );
                context._pAsset->_listNode.push_back( std::move( node ) );
                for ( const XMLNode& child : listChildWidget )
                {
                    uint32 childIndex{ 0 };
                    if ( parseWidget( context, child, childIndex ) == false )
                        return false;
                    context._pAsset->_listNode[outIndex]._listChildIndex.push_back( childIndex );
                }
                return true;
            }

            /** @brief 화면 서술 요소를 읽습니다. 모르는 속성이나 읽지 못한 값은 오류입니다. */
            [[nodiscard]] static bool parseScreenDesc( const ParseContext& context, const XMLNode& element )
            {
                vector<SchemaOrphanValue> listOrphan;
                const SoftReadResult      result = XMLSerializer::deserializeSoft( &context._pAsset->_screenDesc, *UIScreenDesc::StaticType(), element.toString(), &listOrphan );
                return acceptSoftRead( context, element, "UIScreenDesc", result, listOrphan );
            }

            /** @brief 스타일 시트 목록(`<item>경로</item>`)을 읽습니다. */
            [[nodiscard]] static bool parseStyleSheetList( const ParseContext& context, const XMLNode& element )
            {
                for ( XMLNode item = element.findChild(); item.isValid(); item = item.findNextSibling() )
                {
                    if ( StringUtil::equals( item.getName(), kItemElement, true ) == false )
                        return fail( context, item, string( "_listStyleSheet has unknown element <" ) + item.getName() + ">" );
                    const utf8* pPath = item.getText();
                    if ( StringUtil::isNullOrEmpty( pPath ) )
                        return fail( context, item, "_listStyleSheet has an empty item" );
                    context._pAsset->_listStyleSheet.push_back( normalizeDocumentPath( pPath ) );
                }
                return true;
            }

            /** @brief 애니메이션 목록(`<UIAnimation>` 요소들)을 읽습니다. 모르는 필드, 읽지 못한 값, 이름 없음, 겹친 이름은 오류입니다. */
            [[nodiscard]] static bool parseAnimationList( const ParseContext& context, const XMLNode& element )
            {
                UIAnimationList           list{};
                vector<SchemaOrphanValue> listOrphan;
                const string              text   = string( "<UIAnimationList>" ) + element.toString() + "</UIAnimationList>";
                const SoftReadResult      result = XMLSerializer::deserializeSoft( &list, *UIAnimationList::StaticType(), text, &listOrphan );
                if ( acceptSoftRead( context, element, "_listAnimation", result, listOrphan ) == false )
                    return false;
                vector<UIAnimation>& listAnimation = context._pAsset->_listAnimation;
                for ( UIAnimation& animation : list._listAnimation )
                {
                    if ( animation._name.empty() )
                        return fail( context, element, "_listAnimation has an animation without _name" );
                    for ( const UIAnimation& other : listAnimation )
                    {
                        if ( other._name == animation._name )
                            return fail( context, element, string( "_listAnimation has two animations named '" ) + animation._name.c_str() + "'" );
                    }
                    listAnimation.push_back( std::move( animation ) );
                }
                return true;
            }

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

            /** @brief 소프트 읽기에 문제가 없으면 true 입니다. 있으면 모두 적어 실패로 남기고 false 입니다(`describeSoftReadProblem`). */
            [[nodiscard]] static bool acceptSoftRead( const ParseContext& context, const XMLNode& element, const string_view what, const SoftReadResult result,
                                                      const vector<SchemaOrphanValue>& listOrphan )
            {
                const string problem = describeSoftReadProblem( what, result, listOrphan );
                return problem.empty() || fail( context, element, problem );
            }

            static unique_ptr<Widget> failInstantiate( InstantiateContext& context, const UIDocumentAsset& document, const UIDocumentNode& node, string_view message )
            {
                *context._pError = makeError( document._path, node._sourceLine, message );
                return {};
            }

            /** @brief 노드 @p nodeIndex 와 그 아래를 만듭니다. @p namePrefix 가 있으면(조각 안) 이름을 `접두.이름` 으로 감쌉니다. */
            static unique_ptr<Widget> instantiateNode( InstantiateContext& context, const UIDocumentAsset& document, uint32 nodeIndex, const string& namePrefix )
            {
                unique_ptr<Widget>    widget;
                const UIDocumentNode& node               = document._listNode[nodeIndex];
                const TypeInfo*       pType              = engine::getSubsystem<TypeRegistry>().findType( node._typeName );
                const bool            bNotWidgetTypeName = pType == nullptr || pType->isAlive() == false || pType->isDerivedFrom( Widget::StaticType() ) == false;
                if ( bNotWidgetTypeName )
                {
                    widget = failInstantiate( context, document, node, string( "'" ) + node._typeName.c_str() + "' is not a widget type" );
                    return widget;
                }

                widget = UIDocumentLoader::createWidget( *pType );
                if ( widget == nullptr )
                {
                    widget = failInstantiate( context, document, node, string( "'" ) + node._typeName.c_str() + "' cannot be created (abstract or no default constructor)" );
                    return widget;
                }
                const TypeInfo* pDynamicType = widget->getTypeInfo();
                if ( pDynamicType == nullptr || pDynamicType->_name != pType->_name )
                {
                    widget = failInstantiate( context, document, node, string( "'" ) + node._typeName.c_str() + "' does not override getTypeInfo()" );
                    return widget;
                }

                vector<SchemaOrphanValue> listOrphan;
                const SoftReadResult      result  = XMLSerializer::deserializeSoft( widget.get(), *pType, node._propertyXML, &listOrphan );
                const string              problem = describeSoftReadProblem( string( "'" ) + node._typeName.c_str() + "'", result, listOrphan );
                if ( problem.empty() == false )
                {
                    widget = failInstantiate( context, document, node, problem );
                    return widget;
                }

                if ( namePrefix.empty() == false && widget->getName().empty() == false )
                    widget->setName( hashed_string( namePrefix + "." + widget->getName().c_str() ) );
                for ( const UIBindingDesc& binding : node._listBinding )
                {
                    UIBindingDesc instanceBinding = binding;
                    instanceBinding._widget       = widget->getID();
                    context._pOutListBinding->push_back( std::move( instanceBinding ) );
                }

                PanelWidget* pPanel = castTo<PanelWidget>( widget.get() );
                if ( node._fragment.empty() == false )
                {
                    if ( pPanel == nullptr )
                    {
                        widget = failInstantiate( context, document, node, "a fragment needs a panel to hold it" );
                        return widget;
                    }
                    vector<string>& listStack  = context._listFragmentStack;
                    const bool      bRecursive = std::find( listStack.begin(), listStack.end(), node._fragment ) != listStack.end();
                    const bool      bTooDeep   = listStack.size() >= kMaxFragmentDepth;
                    if ( bRecursive || bTooDeep )
                    {
                        string chain;
                        for ( const string& path : listStack )
                        {
                            chain += path + " -> ";
                        }
                        chain += node._fragment;
                        widget = failInstantiate( context, document, node, ( bRecursive ? "fragment includes itself: " : "fragments nest too deep: " ) + chain );
                        return widget;
                    }
                    string                                  fragmentError;
                    const shared_ptr<const UIDocumentAsset> fragment = context._pCache->findOrLoad( node._fragment, fragmentError );
                    if ( fragment == nullptr )
                    {
                        widget = failInstantiate( context, document, node, "fragment '" + node._fragment + "' is not loaded: " + fragmentError );
                        return widget;
                    }
                    // 조각 안의 이름은 이 위젯의 (감싼) 이름으로 감싼다. 이름 없는 조각은 바깥 접두를 그대로 넘긴다.
                    string childPrefix = namePrefix;
                    if ( widget->getName().empty() == false )
                        childPrefix = widget->getName().c_str();
                    listStack.push_back( node._fragment );
                    unique_ptr<Widget> child = instantiateNode( context, *fragment, 0, childPrefix );
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
                    unique_ptr<Widget> child = instantiateNode( context, document, childIndex, namePrefix );
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
    } // namespace
} // namespace sw

namespace sw
{
    bool UIDocumentLoader::parse( string_view text, string_view path, UIDocumentAsset& outAsset, string& outError )
    {
        using Internal = UIDocumentLoaderInternal;
        UIDocumentAsset asset{};
        asset._path = Internal::normalizeDocumentPath( path );

        XMLDocument document;
        if ( document.parse( text, path ) == false )
        {
            outError = document.getLastError();
            return false;
        }
        const Internal::ParseContext context{ text, path, &asset, &outError };
        const XMLNode                root = document.getRoot();
        if ( root.isValid() == false || StringUtil::equals( root.getName(), UIDocumentAsset::kRootElementName, true ) == false )
        {
            outError = Internal::makeError( path, 1, "the root element must be <UIDocument>" );
            return false;
        }

        // 루트 속성은 버전 번호 하나뿐이다. 이전 형식 리더는 없다.
        bool bVersioned{ false };
        for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( StringUtil::equals( attribute.getName(), sw::kSchemaVersionKey, true ) == false )
                return Internal::fail( context, root, string( "UIDocument has unknown attribute '" ) + attribute.getName() + "'" );
            int32 version{ 0 };
            if ( StringUtil::parseInt( string( attribute.getValue() ), version ) == false || version != static_cast<int32>( UIDocumentAsset::kVersion ) )
                return Internal::fail( context, root, string( "_schemaVersion '" ) + attribute.getValue() + "' is not supported (expected " + to_string( UIDocumentAsset::kVersion ) + ")" );
            bVersioned = true;
        }
        if ( bVersioned == false )
            return Internal::fail( context, root, "UIDocument needs _schemaVersion" );

        bool bScreenDesc{ false };
        bool bRootWidget{ false };
        for ( XMLNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, Internal::kScreenDescElement, true ) )
            {
                if ( bScreenDesc )
                    return Internal::fail( context, child, "UIDocument has more than one UIScreenDesc" );
                bScreenDesc = true;
                if ( Internal::parseScreenDesc( context, child ) == false )
                    return false;
                continue;
            }
            if ( StringUtil::equals( pName, Internal::kStyleSheetListElement, true ) )
            {
                if ( Internal::parseStyleSheetList( context, child ) == false )
                    return false;
                continue;
            }
            if ( StringUtil::equals( pName, Internal::kAnimationListElement, true ) )
            {
                if ( Internal::parseAnimationList( context, child ) == false )
                    return false;
                continue;
            }
            if ( Internal::findWidgetType( pName ) == nullptr )
                return Internal::fail( context, child, string( "UIDocument has unknown element <" ) + pName + "> (not a widget type)" );
            if ( bRootWidget )
                return Internal::fail( context, child, "UIDocument has more than one root widget" );
            bRootWidget = true;
            uint32 rootIndex{ 0 };
            if ( Internal::parseWidget( context, child, rootIndex ) == false )
                return false;
        }
        if ( bRootWidget == false )
            return Internal::fail( context, root, "UIDocument has no root widget" );

        outAsset = std::move( asset );
        return true;
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
        void* pMemory = Memory::allocate( type._size );
        if ( pMemory == nullptr )
            return {};
        (void)pCtor->_invoker( pMemory, TaskArgs{} );
        return unique_ptr<Widget>( static_cast<Widget*>( pMemory ) );
    }

    unique_ptr<Widget> UIDocumentLoader::instantiate( const UIDocumentAsset& document, UIDocumentCache& cache, vector<UIBindingDesc>& outListBinding,
                                                      string& outError )
    {
        if ( document._listNode.empty() )
        {
            outError = UIDocumentLoaderInternal::makeError( document._path, 0, "document has no root widget" );
            return {};
        }
        const size_t                                 bindingStart = outListBinding.size();
        UIDocumentLoaderInternal::InstantiateContext context{ &cache, vector<string>{ document._path }, &outListBinding, &outError };
        unique_ptr<Widget>                           root = UIDocumentLoaderInternal::instantiateNode( context, document, 0, string{} );
        if ( root == nullptr )
            outListBinding.resize( bindingStart );
        return root;
    }
} // namespace sw
