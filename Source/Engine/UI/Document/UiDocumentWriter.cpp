#include "pch.h"

#include "Engine/UI/Document/UiDocumentWriter.h"

#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/XmlSerializer.h"
#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Document/UiDocument.h"
#include "Engine/UI/Document/UiDocumentLoader.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/Widgets/UserWidget.h"
#include "Engine/Utility/Xml/XmlDocument.h"

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
            [[nodiscard]] static bool writeDifference( const XmlNode& current, const XmlNode& defaults, const XmlNode& destination, const TypeInfo& type )
            {
                bool bWritten{ false };
                for ( XmlAttribute attribute = current.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const utf8* pDefault = defaults.isValid() ? defaults.findAttribute( attribute.getName() ) : nullptr;
                    if ( pDefault != nullptr && StringUtil::equals( pDefault, attribute.getValue() ) )
                        continue;
                    destination.appendAttribute( attribute.getName(), attribute.getValue() );
                    bWritten = true;
                }
                for ( XmlNode child = current.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    const XmlNode       defaultChild = defaults.isValid() ? defaults.findChild( child.getName() ) : XmlNode{};
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
                    XmlDocument   scratch;
                    const XmlNode scratchElement = scratch.appendRoot( child.getName() );
                    if ( writeDifference( child, defaultChild, scratchElement, *pNestedType ) )
                    {
                        (void)destination.appendClone( scratchElement );
                        bWritten = true;
                    }
                }
                return bWritten;
            }

            /** @brief 원소 @p element 아래 경로 @p path(`_slot._offsetMin`)의 속성에 @p value 를 적습니다. 가는 길의 원소가 없으면 만듭니다. */
            static void writeAtPath( const XmlNode& element, string_view path, const string& value )
            {
                XmlNode     target = element;
                string_view rest   = path;
                for ( size_t dot = rest.find( '.' ); dot != string_view::npos; dot = rest.find( '.' ) )
                {
                    const string segment( rest.substr( 0, dot ) );
                    XmlNode      child = target.findChild( segment.c_str() );
                    if ( child.isValid() == false )
                        child = target.appendChild( segment.c_str() );
                    target = child;
                    rest.remove_prefix( dot + 1 );
                }
                target.setAttribute( string( rest ).c_str(), value.c_str() );
            }

            /** @brief 위젯 @p widget 과 그 자식을 @p parent 아래 원소로 씁니다. */
            static void writeWidget( const Widget& widget, const XmlNode& parent, const vector<UiBindingDesc>& listBinding )
            {
                const TypeInfo* pType = widget.getTypeInfo();
                if ( pType == nullptr )
                    return;
                const XmlNode element = parent.appendChild( pType->_name.c_str() );

                XmlDocument currentDocument;
                XmlDocument defaultDocument;
                if ( currentDocument.parse( XmlSerializer::serialize( &widget, *pType ) ) )
                {
                    const unique_ptr<Widget> defaults  = UiDocumentLoader::createWidget( *pType );
                    const bool               bDefaults = defaults != nullptr && defaultDocument.parse( XmlSerializer::serialize( defaults.get(), *pType ) );
                    // 다른 칸이 없어도 위젯 원소는 남는다(타입이 곧 내용이다) — 적었는지는 쓰지 않는다
                    (void)writeDifference( currentDocument.getRoot(), bDefaults ? defaultDocument.getRoot() : XmlNode{}, element, *pType );
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
                    writeWidget( *pPanel->getChild( index ), element, listBinding );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string UiDocumentWriter::write( const UiScreenDesc& desc, const vector<string>& listStyleSheet, const Widget& root, const vector<UiBindingDesc>& listBinding )
    {
        using Internal = UiDocumentWriterInternal;
        XmlDocument   document;
        const XmlNode documentRoot = document.appendRoot( UiDocumentAsset::kRootElementName );
        documentRoot.appendAttribute( "_schemaVersion", UiDocumentAsset::kVersion );

        // 화면 서술 — 기본값과 다른 칸이 있을 때만.
        XmlDocument        currentDesc;
        XmlDocument        defaultDesc;
        const UiScreenDesc defaults{};
        const TypeInfo&    descType = *UiScreenDesc::StaticType();
        if ( currentDesc.parse( XmlSerializer::serialize( &desc, descType ) ) && defaultDesc.parse( XmlSerializer::serialize( &defaults, descType ) ) )
        {
            XmlDocument   scratch;
            const XmlNode scratchElement = scratch.appendRoot( descType._name.c_str() );
            if ( Internal::writeDifference( currentDesc.getRoot(), defaultDesc.getRoot(), scratchElement, descType ) )
                (void)documentRoot.appendClone( scratchElement );
        }

        if ( listStyleSheet.empty() == false )
        {
            const XmlNode list = documentRoot.appendChild( "_listStyleSheet" );
            for ( const string& path : listStyleSheet )
                list.appendChild( "item" ).setValue( string_view( path ) );
        }

        Internal::writeWidget( root, documentRoot, listBinding );
        return document.saveToString();
    }
} // namespace sw
