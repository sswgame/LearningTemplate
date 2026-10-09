#include "pch.h"

#include "Engine/UI/Style/UiStyleSheet.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Serialization/Xml/XmlDocument.h"
#include "Engine/UI/Animation/UiStyleTransition.h"
#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/Widget.h"

namespace sw
{
    namespace
    {
        struct UiStyleSheetInternal
        {
            static constexpr utf8 kVariableElement[] = "Variable";
            static constexpr utf8 kRuleElement[]     = "Rule";
            static constexpr utf8 kNameAttribute[]   = "_name";
            static constexpr utf8 kValueAttribute[]  = "_value";
            static constexpr utf8 kSelectorKey[]     = "_selector";
            static constexpr utf8 kVariablePrefix    = '$';

            /** @brief 특정도 자리(이름 · 클래스와 상태 · 타입)의 비트 이동입니다. */
            static constexpr uint32 kNameShift  = 16;
            static constexpr uint32 kClassShift = 8;

            struct StateName
            {
                const utf8* _pName;
                uint32      _bit;
            };
            static constexpr StateName kArrStateName[] = {
                {        "hover",        UiStyleState::kHover},
                {      "pressed",      UiStyleState::kPressed},
                {        "focus",        UiStyleState::kFocus},
                {"focus-visible", UiStyleState::kFocusVisible},
                {     "disabled",     UiStyleState::kDisabled},
                {      "checked",      UiStyleState::kChecked},
                {     "selected",     UiStyleState::kSelected},
            };

            struct Variable
            {
                string _name;
                string _value;
            };

            /** @brief 파싱 한 번의 문맥입니다. */
            struct ParseContext
            {
                string_view        _text;
                string_view        _path;
                vector<Variable>   _listVariable;
                UiStyleSheetAsset* _pSheet;
                string*            _pError;
            };

            static bool isIdentifierChar( utf8 ch ) { return std::isalnum( static_cast<uint8>( ch ) ) != 0 || ch == '_' || ch == '-'; }

            /** @brief @p text 의 @p inoutPos 부터 이름 글자를 읽습니다. 하나도 없으면 빈 글입니다. */
            static string_view readIdentifier( string_view text, size_t& inoutPos )
            {
                const size_t start = inoutPos;
                while ( inoutPos < text.size() && isIdentifierChar( text[inoutPos] ) )
                {
                    ++inoutPos;
                }
                return text.substr( start, inoutPos - start );
            }

            /** @brief 복합 선택자 하나를 읽습니다. */
            [[nodiscard]] static bool parsePart( string_view text, UiStyleSelectorPart& outPart, uint32& inoutSpecificity, string& outError )
            {
                size_t position = 0;
                if ( text.empty() == false && std::isalpha( static_cast<uint8>( text[0] ) ) != 0 )
                {
                    outPart._typeName = hashed_string( readIdentifier( text, position ) );
                    inoutSpecificity += 1;
                }
                while ( position < text.size() )
                {
                    const utf8        marker     = text[position++];
                    const string_view identifier = readIdentifier( text, position );
                    if ( identifier.empty() || ( marker != '.' && marker != '#' && marker != ':' ) )
                    {
                        outError = "selector '" + string( text ) + "' has unsupported syntax at '" + string( text.substr( position - 1 ) ) + "'";
                        return false;
                    }
                    if ( marker == '.' )
                    {
                        outPart._listClass.push_back( hashed_string( identifier ) );
                        inoutSpecificity += 1u << kClassShift;
                    }
                    else if ( marker == '#' )
                    {
                        if ( outPart._name.empty() == false )
                        {
                            outError = "selector '" + string( text ) + "' has two names";
                            return false;
                        }
                        outPart._name = hashed_string( identifier );
                        inoutSpecificity += 1u << kNameShift;
                    }
                    else
                    {
                        uint32 bit{ 0 };
                        for ( const StateName& state : kArrStateName )
                        {
                            if ( StringUtil::equals( identifier, string_view( state._pName ) ) )
                                bit = state._bit;
                        }
                        if ( bit == 0 )
                        {
                            outError = "selector '" + string( text ) + "' has unknown state ':" + string( identifier ) + "'";
                            return false;
                        }
                        outPart._stateMask |= bit;
                        inoutSpecificity += 1u << kClassShift;
                    }
                }
                return true;
            }

            /** @brief 위젯의 스타일 클래스(빈 칸으로 나눈 이름들)에 @p className 이 있는가입니다. */
            static bool hasStyleClass( const string& styleClass, const hashed_string& className )
            {
                const string_view wanted( className.c_str() );
                size_t            position = 0;
                while ( position < styleClass.size() )
                {
                    while ( position < styleClass.size() && styleClass[position] == ' ' )
                    {
                        ++position;
                    }
                    const size_t start = position;
                    while ( position < styleClass.size() && styleClass[position] != ' ' )
                    {
                        ++position;
                    }
                    if ( position > start && string_view( styleClass ).substr( start, position - start ) == wanted )
                        return true;
                }
                return false;
            }

            static string makeError( string_view path, uint32 line, string_view message )
            {
                string error( path );
                if ( line > 0 )
                    error += ":" + to_string( line );
                error += ": ";
                error += message;
                return error;
            }

            static bool fail( const ParseContext& context, const XmlNode& node, string_view message )
            {
                *context._pError = makeError( context._path, XmlDocument::computeLineNumber( context._text, node.getSourceOffset() ), message );
                return false;
            }

            /** @brief 값 글의 `$변수` 를 풉니다. 모르는 변수면 false 입니다. */
            [[nodiscard]] static bool resolveValue( const ParseContext& context, const utf8* pValue, string& outText )
            {
                if ( pValue == nullptr || pValue[0] != kVariablePrefix )
                {
                    outText = pValue != nullptr ? pValue : "";
                    return true;
                }
                const string_view name( pValue + 1 );
                for ( const Variable& variable : context._listVariable )
                {
                    if ( variable._name == name )
                    {
                        outText = variable._value;
                        return true;
                    }
                }
                return false;
            }

            /** @brief @p type 의 프로퍼티 중 이름이 @p pName 인 것입니다(대소문자 무시). */
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

            /** @brief 칸 하나를 만들고 값이 그 타입으로 읽히는지 본다(빈 스타일에 써 본다). */
            [[nodiscard]] static bool addAssignment( const ParseContext& context, const XmlNode& node, UiStyleRule& inoutRule, UiStyleField field,
                                                     const PropertyInfo& property, const PropertyInfo* pNested, const utf8* pName, const utf8* pValue )
            {
                UiStyleAssignment assignment{};
                assignment._field           = field;
                assignment._pProperty       = &property;
                assignment._pNestedProperty = pNested;
                if ( resolveValue( context, pValue, assignment._text ) == false )
                    return fail( context, node, string( "Rule uses unknown variable '" ) + pValue + "'" );
                WidgetStyle scratch{};
                void*       pTarget = pNested != nullptr ? property.getRawPtr( &scratch ) : &scratch;
                const bool  bRead   = SerializerUtil::applyPropertyText( pNested != nullptr ? *pNested : property, pTarget, assignment._text, SerializeContext::getDefault() );
                if ( bRead == false )
                    return fail( context, node, string( "Rule value '" ) + assignment._text + "' cannot be read as '" + pName + "'" );
                if ( field == UiStyleField::Transition )
                {
                    UiStyleTransitionSpec spec{};
                    string                transitionError;
                    if ( UiStyleTransitionSpec::parse( assignment._text, spec, transitionError ) == false )
                        return fail( context, node, "Rule _transition: " + transitionError );
                }
                inoutRule._listAssignment.push_back( std::move( assignment ) );
                return true;
            }

            /** @brief 규칙 원소 하나를 읽습니다. */
            [[nodiscard]] static bool parseRule( ParseContext& context, const XmlNode& element )
            {
                UiStyleRule rule{};
                rule._sourceLine      = XmlDocument::computeLineNumber( context._text, element.getSourceOffset() );
                const utf8* pSelector = element.findAttribute( kSelectorKey );
                if ( StringUtil::isNullOrEmpty( pSelector ) )
                    return fail( context, element, "Rule needs _selector" );
                string selectorError;
                if ( UiStyleSelector::parse( pSelector, rule._selector, selectorError ) == false )
                    return fail( context, element, selectorError );

                const TypeInfo& styleType = *WidgetStyle::StaticType();
                for ( XmlAttribute attribute = element.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    const utf8* pName = attribute.getName();
                    if ( StringUtil::equals( pName, kSelectorKey, true ) )
                        continue;
                    UiStyleField        field{};
                    const PropertyInfo* pProperty = findProperty( styleType, pName );
                    if ( pProperty == nullptr || UiStyleFieldTable::tryFindField( pName, field ) == false )
                        return fail( context, element, string( "Rule has unknown style property '" ) + pName + "'" );
                    if ( addAssignment( context, element, rule, field, *pProperty, nullptr, pName, attribute.getValue() ) == false )
                        return false;
                }
                for ( XmlNode child = element.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    const utf8*         pName = child.getName();
                    UiStyleField        field{};
                    const PropertyInfo* pProperty = findProperty( styleType, pName );
                    const TypeInfo*     pNested   = pProperty != nullptr ? SerializerUtil::findNestedObjectType( pProperty->_typeName, SerializeContext::getDefault() ) : nullptr;
                    if ( pNested == nullptr || UiStyleFieldTable::tryFindField( pName, field ) == false )
                        return fail( context, child, string( "Rule has unknown style element <" ) + pName + ">" );
                    for ( XmlAttribute attribute = child.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                    {
                        const PropertyInfo* pInner = findProperty( *pNested, attribute.getName() );
                        if ( pInner == nullptr )
                            return fail( context, child, string( "<" ) + pName + "> has unknown attribute '" + attribute.getName() + "'" );
                        if ( addAssignment( context, child, rule, field, *pProperty, pInner, attribute.getName(), attribute.getValue() ) == false )
                            return false;
                    }
                }
                context._pSheet->_listRule.push_back( std::move( rule ) );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool UiStyleSelector::parse( string_view text, UiStyleSelector& outSelector, string& outError )
    {
        outSelector     = UiStyleSelector{};
        size_t position = 0;
        while ( position < text.size() )
        {
            while ( position < text.size() && text[position] == ' ' )
            {
                ++position;
            }
            const size_t start = position;
            while ( position < text.size() && text[position] != ' ' )
            {
                ++position;
            }
            if ( position == start )
                break;
            UiStyleSelectorPart part{};
            if ( UiStyleSheetInternal::parsePart( text.substr( start, position - start ), part, outSelector._specificity, outError ) == false )
                return false;
            outSelector._listPart.push_back( std::move( part ) );
        }
        if ( outSelector._listPart.empty() )
        {
            outError = "selector is empty";
            return false;
        }
        return true;
    }

    bool UiStyleSelector::matchesPart( const UiStyleSelectorPart& part, const Widget& widget, bool bNavigationMode )
    {
        if ( part._typeName.empty() == false )
        {
            const TypeInfo* pType = widget.getTypeInfo();
            if ( pType == nullptr || pType->_name != part._typeName )
                return false;
        }
        if ( part._name.empty() == false && widget.getName() != part._name )
            return false;
        for ( const hashed_string& className : part._listClass )
        {
            if ( UiStyleSheetInternal::hasStyleClass( widget.getStyleClass(), className ) == false )
                return false;
        }
        if ( part._stateMask == 0 )
            return true;
        uint32 states = widget.computeStyleStates();
        if ( bNavigationMode && ( states & UiStyleState::kFocus ) != 0 )
            states |= UiStyleState::kFocusVisible;
        return ( states & part._stateMask ) == part._stateMask;
    }

    bool UiStyleSelector::matches( const Widget& widget, bool bNavigationMode ) const
    {
        if ( _listPart.empty() || matchesPart( _listPart.back(), widget, bNavigationMode ) == false )
            return false;
        const Widget* pAncestor = widget.getParent();
        for ( size_t index = _listPart.size() - 1; index > 0; --index )
        {
            const UiStyleSelectorPart& part = _listPart[index - 1];
            while ( pAncestor != nullptr && matchesPart( part, *pAncestor, bNavigationMode ) == false )
            {
                pAncestor = pAncestor->getParent();
            }
            if ( pAncestor == nullptr )
                return false;
            pAncestor = pAncestor->getParent();
        }
        return true;
    }

    bool UiStyleSheetLoader::parse( string_view text, string_view path, UiStyleSheetAsset& outSheet, string& outError )
    {
        using Internal = UiStyleSheetInternal;
        UiStyleSheetAsset sheet{};
        sheet._path = FileUtil::normalizePath( path );
        XmlDocument document;
        if ( document.parse( text, path ) == false )
        {
            outError = document.getLastError();
            return false;
        }
        Internal::ParseContext context{ text, path, {}, &sheet, &outError };
        const XmlNode          root = document.getRoot();
        if ( root.isValid() == false || StringUtil::equals( root.getName(), UiStyleSheetAsset::kRootElementName, true ) == false )
        {
            outError = Internal::makeError( path, 1, "the root element must be <UiStyleSheet>" );
            return false;
        }
        bool bVersioned{ false };
        for ( XmlAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( StringUtil::equals( attribute.getName(), sw::kSchemaVersionKey, true ) == false )
                return Internal::fail( context, root, string( "UiStyleSheet has unknown attribute '" ) + attribute.getName() + "'" );
            int32 version{ 0 };
            if ( StringUtil::parseInt( string( attribute.getValue() ), version ) == false || version != static_cast<int32>( UiStyleSheetAsset::kVersion ) )
                return Internal::fail( context, root, string( "_schemaVersion '" ) + attribute.getValue() + "' is not supported (expected " + to_string( UiStyleSheetAsset::kVersion ) + ")" );
            bVersioned = true;
        }
        if ( bVersioned == false )
            return Internal::fail( context, root, "UiStyleSheet needs _schemaVersion" );

        // 변수는 시트 안 어디에 적든 쓴다 — 먼저 모은다.
        for ( XmlNode child = root.findChild( Internal::kVariableElement ); child.isValid(); child = child.findNextSibling( Internal::kVariableElement ) )
        {
            const utf8* pName  = child.findAttribute( Internal::kNameAttribute );
            const utf8* pValue = child.findAttribute( Internal::kValueAttribute );
            if ( StringUtil::isNullOrEmpty( pName ) || pValue == nullptr )
                return Internal::fail( context, child, "Variable needs _name and _value" );
            context._listVariable.push_back( Internal::Variable{ string( pName ), string( pValue ) } );
        }
        for ( XmlNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, Internal::kVariableElement, true ) )
                continue;
            if ( StringUtil::equals( pName, Internal::kRuleElement, true ) == false )
                return Internal::fail( context, child, string( "UiStyleSheet has unknown element <" ) + pName + ">" );
            if ( Internal::parseRule( context, child ) == false )
                return false;
        }
        outSheet = std::move( sheet );
        return true;
    }

    void UiStyleSheetLoader::applyAssignment( const UiStyleAssignment& assignment, WidgetStyle& inoutStyle )
    {
        if ( assignment._pProperty == nullptr )
            return;
        if ( assignment._pNestedProperty == nullptr )
        {
            (void)SerializerUtil::applyPropertyText( *assignment._pProperty, &inoutStyle, assignment._text, SerializeContext::getDefault() ); // 읽을 때 확인했다
            return;
        }
        void* pNested = assignment._pProperty->getRawPtr( &inoutStyle );
        (void)SerializerUtil::applyPropertyText( *assignment._pNestedProperty, pNested, assignment._text, SerializeContext::getDefault() ); // 읽을 때 확인했다
    }
} // namespace sw
