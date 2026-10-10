#include "pch.h"

#include "Engine/UI/Document/UiDocumentWriter.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/UI/Animation/UiAnimation.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Document/UiDocument.h"
#include "Engine/UI/Document/UiDocumentLoader.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/Widgets/UserWidget.h"

namespace sw
{
    namespace
    {
        struct UiDocumentWriterInternal
        {
            /** @brief @p type(상속 포함)의 프로퍼티 중 이름이 @p pName 인 것입니다(대소문자 무시). 없으면 nullptr 입니다. */
            static const PropertyInfo* findProperty( const TypeInfo& type, const utf8* pName )
            {
                const uint32 nameHash = static_cast<uint32>( hashed_string::computeHash( string_view( pName ) ) );
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( prop.matchesNameHash( nameHash ) )
                        return &prop;
                }
                return nullptr;
            }

            /**
             * @brief 지금 값 원소 @p current 중 기본값 원소 @p defaults 와 다른 것만 @p destination 에 옮겨 적습니다. 무엇이든 적었으면 true 입니다.
             * @details 속성은 하나씩, 구조체 칸 원소는 재귀로 칸마다, 컨테이너 칸 원소는 다르면 통째로 적습니다(원소 순서가 뜻이다).
             */
            [[nodiscard]] static bool writeDifference( const XMLNode& current, const XMLNode& defaults, const XMLNode& destination, const TypeInfo& type )
            {
                bool bWritten{ false };
                for ( XMLAttribute attribute = current.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const utf8* pDefault = defaults.isValid() ? defaults.findAttribute( attribute.getName() ) : nullptr;
                    if ( pDefault != nullptr && StringUtil::equals( pDefault, attribute.getValue() ) )
                        continue;
                    destination.appendAttribute( attribute.getName(), attribute.getValue() );
                    bWritten = true;
                }
                for ( XMLNode child = current.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    const XMLNode       defaultChild = defaults.isValid() ? defaults.findChild( child.getName() ) : XMLNode{};
                    const PropertyInfo* pProperty    = findProperty( type, child.getName() );
                    const bool          bStruct      = pProperty != nullptr && pProperty->_bIsContainer == SW_FALSE;
                    const TypeInfo*     pNestedType  = bStruct ? engine::getTypeRegistry().findType( pProperty->_typeName ) : nullptr;
                    if ( pNestedType == nullptr )
                    {
                        if ( defaultChild.isValid() && child.toString() == defaultChild.toString() )
                            continue;
                        (void)destination.appendClone( child );
                        bWritten = true;
                        continue;
                    }
                    // 구조체 칸 — 다른 칸이 있을 때만 원소를 남긴다.
                    XMLDocument   scratch;
                    const XMLNode scratchElement = scratch.appendRoot( child.getName() );
                    if ( writeDifference( child, defaultChild, scratchElement, *pNestedType ) )
                    {
                        (void)destination.appendClone( scratchElement );
                        bWritten = true;
                    }
                }
                return bWritten;
            }

            /** @brief 원소 @p element 아래 경로 @p path(`_slot._offsetMin`)의 속성에 @p value 를 적습니다. 가는 길의 원소가 없으면 만듭니다. */
            static void writeAtPath( const XMLNode& element, string_view path, const string& value )
            {
                XMLNode     target = element;
                string_view rest   = path;
                for ( size_t dot = rest.find( '.' ); dot != string_view::npos; dot = rest.find( '.' ) )
                {
                    const string segment( rest.substr( 0, dot ) );
                    XMLNode      child = target.findChild( segment.c_str() );
                    if ( child.isValid() == false )
                        child = target.appendChild( segment.c_str() );
                    target = child;
                    rest.remove_prefix( dot + 1 );
                }
                target.setAttribute( string( rest ).c_str(), value.c_str() );
            }

            /** @brief @p source 를 @p destinationParent 아래로 옮겨 적되, 속성 · 자식 · 글이 없는 원소(빈 컨테이너 `<_listEvent />`)는 뺍니다 — 손으로 쓴 문서와 같은 모양. */
            static void copyWithoutEmptyElements( const XMLNode& source, const XMLNode& destinationParent )
            {
                const XMLNode destination = destinationParent.appendChild( source.getName() );
                for ( XMLAttribute attribute = source.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    destination.appendAttribute( attribute.getName(), attribute.getValue() );
                }
                for ( XMLNode child = source.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    const bool bEmpty = child.getFirstAttribute().isValid() == false && child.findChild().isValid() == false && StringUtil::isNullOrEmpty( child.getText() );
                    if ( bEmpty == false )
                        copyWithoutEmptyElements( child, destination );
                }
            }

            /** @brief 위젯 @p widget 과 그 자식을 @p parent 아래 원소로 씁니다. */
            static void writeWidget( const Widget& widget, const XMLNode& parent, const vector<UiBindingDesc>& listBinding )
            {
                const TypeInfo* pType = widget.getTypeInfo();
                if ( pType == nullptr )
                    return;
                const XMLNode element = parent.appendChild( pType->_name.c_str() );

                XMLDocument currentDocument;
                XMLDocument defaultDocument;
                if ( currentDocument.parse( XMLSerializer::serialize( &widget, *pType ) ) )
                {
                    const unique_ptr<Widget> defaults  = UiDocumentLoader::createWidget( *pType );
                    const bool               bDefaults = defaults != nullptr && defaultDocument.parse( XMLSerializer::serialize( defaults.get(), *pType ) );
                    // 다른 칸이 없어도 위젯 원소는 남는다(타입이 곧 내용이다) — 적었는지는 쓰지 않는다
                    (void)writeDifference( currentDocument.getRoot(), bDefaults ? defaultDocument.getRoot() : XMLNode{}, element, *pType );
                }
                for ( const UiBindingDesc& binding : listBinding )
                {
                    if ( binding._widget == widget.getId() )
                        writeAtPath( element, binding._propertyPath, binding._expression );
                }

                // 조각의 내용은 조각 문서의 것이다 — 원소만 쓴다.
                const PanelWidget* pPanel = castTo<const PanelWidget>( &widget );
                if ( pPanel == nullptr || castTo<const UserWidget>( &widget ) != nullptr )
                    return;
                for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
                {
                    writeWidget( *pPanel->getChild( index ), element, listBinding );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string UiDocumentWriter::write( const UiScreenDesc& desc, const vector<string>& listStyleSheet, const Widget& root, const vector<UiBindingDesc>& listBinding,
                                    const vector<UiAnimation>& listAnimation )
    {
        using Internal = UiDocumentWriterInternal;
        XMLDocument   document;
        const XMLNode documentRoot = document.appendRoot( UiDocumentAsset::kRootElementName );
        documentRoot.appendAttribute( kSchemaVersionKey, UiDocumentAsset::kVersion );

        // 화면 서술 — 기본값과 다른 칸이 있을 때만.
        XMLDocument        currentDesc;
        XMLDocument        defaultDesc;
        const UiScreenDesc defaults{};
        const TypeInfo&    descType = *UiScreenDesc::StaticType();
        if ( currentDesc.parse( XMLSerializer::serialize( &desc, descType ) ) && defaultDesc.parse( XMLSerializer::serialize( &defaults, descType ) ) )
        {
            XMLDocument   scratch;
            const XMLNode scratchElement = scratch.appendRoot( descType._name.c_str() );
            if ( Internal::writeDifference( currentDesc.getRoot(), defaultDesc.getRoot(), scratchElement, descType ) )
                (void)documentRoot.appendClone( scratchElement );
        }

        if ( listStyleSheet.empty() == false )
        {
            const XMLNode list = documentRoot.appendChild( "_listStyleSheet" );
            for ( const string& path : listStyleSheet )
            {
                list.appendChild( "item" ).setValue( string_view( path ) );
            }
        }

        // 애니메이션 — 그릇(UiAnimationList)으로 직렬화한 `_listAnimation` 원소를 그대로 옮긴다.
        if ( listAnimation.empty() == false )
        {
            UiAnimationList holder{};
            holder._listAnimation = listAnimation;
            XMLDocument animationDocument;
            if ( animationDocument.parse( XMLSerializer::serialize( &holder, *UiAnimationList::StaticType() ) ) )
            {
                const XMLNode list = animationDocument.getRoot().findChild( "_listAnimation" );
                if ( list.isValid() )
                    Internal::copyWithoutEmptyElements( list, documentRoot );
            }
        }

        Internal::writeWidget( root, documentRoot, listBinding );
        return document.saveToString();
    }
} // namespace sw
