#include "pch.h"

#include "Engine/UI/Style/UIStyleSheet.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/UI/Animation/UIStyleTransitionState.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"

namespace sw
{
    namespace
    {
        struct UIStyleSheetInternal
        {
            static constexpr utf8 kVariableElement[]  = "Variable";
            static constexpr utf8 kRuleElement[]      = "Rule";
            static constexpr utf8 kVariationElement[] = "TypeVariation";
            static constexpr utf8 kExtendsAttribute[] = "_extends";
            static constexpr utf8 kNameAttribute[]    = "_name";
            static constexpr utf8 kValueAttribute[]   = "_value";
            static constexpr utf8 kSelectorKey[]      = "_selector";
            static constexpr utf8 kVariablePrefix     = '$';

            /** @brief 특정도 항목(이름, 클래스와 상태, 타입)의 비트 이동량입니다. */
            static constexpr uint32 kNameShift  = 16;
            static constexpr uint32 kClassShift = 8;

            struct StateName
            {
                const utf8* _pName;
                uint32      _bit;
            };
            static constexpr StateName kArrStateName[] = {
                {        "hover",        UIStyleState::kHover},
                {      "pressed",      UIStyleState::kPressed},
                {        "focus",        UIStyleState::kFocus},
                {"focus-visible", UIStyleState::kFocusVisible},
                {     "disabled",     UIStyleState::kDisabled},
                {      "checked",      UIStyleState::kChecked},
                {     "selected",     UIStyleState::kSelected},
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
                UIStyleSheetAsset* _pSheet;
                string*            _pError;
                bool               _bFlattened; ///< 부모를 합친 글을 읽었다 — 노드의 위치가 @p _text 의 위치가 아니라 줄 번호를 적지 않는다 NOLINT(sw-bool-flag): 집합체 초기화로 만드는 지역 문맥이다

                uint32 findLine( const XMLNode& node ) const
                {
                    return _bFlattened ? 0u : XMLDocument::computeLineNumber( _text, node.getSourceOffset() );
                }

                bool fail( const XMLNode& node, string_view message ) const
                {
                    *_pError = makeError( _path, findLine( node ), message );
                    return false;
                }

                /**
                 * @brief 형식 변형 하나를 규칙으로 풉니다. @p baseClass 를 쓰는 모든 규칙(`:hover` 같은 상태와 자손 선택자 포함)을 @p variationClass 로 바꿔 복제해 시트 맨 앞에 둡니다.
                 * @details 맨 앞이라 같은 특정도에서 시트에 적은 변형 자신의 규칙이 이긴다 — 차이만 적으면 된다(Godot Theme Type Variation 의 base type, USS 는 이 기능이 없어 클래스를 둘 다 적는다).
                 *          읽을 때 끝나므로 선택자 비교 · 위젯 쪽 코드가 변형을 모른다. 앞서 푼 변형의 복제도 다시 보므로 변형의 변형은 선언 순서대로 적는다.
                 * @return 부모 클래스를 쓰는 규칙이 없으면(철자 오류) 오류이고 false 입니다.
                 */
                [[nodiscard]] bool expandTypeVariation( const XMLNode& element )
                {
                    const utf8* pName    = element.findAttribute( kNameAttribute );
                    const utf8* pExtends = element.findAttribute( kExtendsAttribute );
                    if ( StringUtil::isNullOrEmpty( pName ) || StringUtil::isNullOrEmpty( pExtends ) || StringUtil::equals( pName, pExtends ) )
                        return fail( element, "TypeVariation needs _name and a different _extends" );
                    const hashed_string variationClass( pName );
                    const hashed_string baseClass( pExtends );
                    vector<UIStyleRule> listRule;
                    for ( const UIStyleRule& rule : _pSheet->_listRule )
                    {
                        UIStyleRule clone = rule;
                        bool        bUses{ false };
                        for ( UIStyleSelectorPart& part : clone._selector._listPart )
                        {
                            for ( hashed_string& className : part._listClass )
                            {
                                if ( className == baseClass )
                                {
                                    className = variationClass;
                                    bUses     = true;
                                }
                            }
                        }
                        if ( bUses )
                            listRule.push_back( std::move( clone ) );
                    }
                    if ( listRule.empty() )
                        return fail( element, string( "TypeVariation '" ) + pName + "' extends class '" + pExtends + "' that no rule uses" );
                    for ( UIStyleRule& rule : _pSheet->_listRule )
                    {
                        listRule.push_back( std::move( rule ) );
                    }
                    _pSheet->_listRule = std::move( listRule );
                    return true;
                }

                /** @brief 값 텍스트의 `$변수` 를 해석합니다. 모르는 변수면 false 입니다. */
                [[nodiscard]] bool resolveValue( const utf8* pValue, string& outText ) const
                {
                    if ( pValue == nullptr || pValue[0] != kVariablePrefix )
                    {
                        outText = pValue != nullptr ? pValue : "";
                        return true;
                    }
                    const string_view name( pValue + 1 );
                    for ( const Variable& variable : _listVariable )
                    {
                        if ( variable._name == name )
                        {
                            outText = variable._value;
                            return true;
                        }
                    }
                    return false;
                }

                /** @brief 필드 하나를 만들고 값이 그 타입으로 읽히는지 확인합니다(빈 스타일에 써 봅니다). */
                [[nodiscard]] bool addAssignment( const XMLNode& node, UIStyleRule& inoutRule, UIStyleField field,
                                                  const PropertyInfo& property, const PropertyInfo* pNested, const utf8* pName, const utf8* pValue ) const
                {
                    UIStyleAssignment assignment{};
                    assignment._field           = field;
                    assignment._pProperty       = &property;
                    assignment._pNestedProperty = pNested;
                    if ( resolveValue( pValue, assignment._text ) == false )
                        return fail( node, string( "Rule uses unknown variable '" ) + pValue + "'" );
                    WidgetStyle scratch{};
                    void*       pTarget = pNested != nullptr ? property.getRawPtr( &scratch ) : &scratch;
                    const bool  bRead   = SerializerUtil::applyPropertyText( pNested != nullptr ? *pNested : property, MutableObjectAddress{ pTarget }, assignment._text, SerializeContext::getDefault( SerializePurpose::Authoring ) );
                    if ( bRead == false )
                        return fail( node, string( "Rule value '" ) + assignment._text + "' cannot be read as '" + pName + "'" );
                    if ( field == UIStyleField::Transition )
                    {
                        UIStyleTransitionSpec spec{};
                        string                transitionError;
                        if ( UIStyleTransitionSpec::parse( assignment._text, spec, transitionError ) == false )
                            return fail( node, "Rule _transition: " + transitionError );
                    }
                    inoutRule._listAssignment.push_back( std::move( assignment ) );
                    return true;
                }

                /** @brief 규칙 원소 하나를 읽습니다. */
                [[nodiscard]] bool parseRule( const XMLNode& element )
                {
                    UIStyleRule rule{};
                    rule._sourceLine      = findLine( element );
                    const utf8* pSelector = element.findAttribute( kSelectorKey );
                    if ( StringUtil::isNullOrEmpty( pSelector ) )
                        return fail( element, "Rule needs _selector" );
                    string selectorError;
                    if ( UIStyleSelector::parse( pSelector, rule._selector, selectorError ) == false )
                        return fail( element, selectorError );

                    const TypeInfo& styleType = *WidgetStyle::StaticType();
                    for ( XMLAttribute attribute = element.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                    {
                        const utf8* pName = attribute.getName();
                        if ( StringUtil::equals( pName, kSelectorKey ) )
                            continue;
                        UIStyleField        field{};
                        const PropertyInfo* pProperty = findProperty( styleType, pName );
                        if ( pProperty == nullptr || UIStyleFieldTable::tryFindField( pName, field ) == false )
                            return fail( element, string( "Rule has unknown style property '" ) + pName + "'" );
                        if ( addAssignment( element, rule, field, *pProperty, nullptr, pName, attribute.getValue() ) == false )
                            return false;
                    }
                    for ( XMLNode child = element.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        const utf8*         pName = child.getName();
                        UIStyleField        field{};
                        const PropertyInfo* pProperty = findProperty( styleType, pName );
                        const TypeInfo*     pNested   = pProperty != nullptr ? SerializerUtil::findNestedObjectType( pProperty->_typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) ) : nullptr;
                        if ( pNested == nullptr || UIStyleFieldTable::tryFindField( pName, field ) == false )
                            return fail( child, string( "Rule has unknown style element <" ) + pName + ">" );
                        for ( XMLAttribute attribute = child.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                        {
                            const PropertyInfo* pInner = findProperty( *pNested, attribute.getName() );
                            if ( pInner == nullptr )
                                return fail( child, string( "<" ) + pName + "> has unknown attribute '" + attribute.getName() + "'" );
                            if ( addAssignment( child, rule, field, *pProperty, pInner, attribute.getName(), attribute.getValue() ) == false )
                                return false;
                        }
                    }
                    _pSheet->_listRule.push_back( std::move( rule ) );
                    return true;
                }
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
            [[nodiscard]] static bool parsePart( string_view text, UIStyleSelectorPart& outPart, uint32& inoutSpecificity, string& outError )
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

            /** @brief 위젯의 스타일 클래스(공백으로 나눈 이름들)에 @p className 이 있으면 true 입니다. */
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
                    error += formatToString( ":%#", line );
                error += ": ";
                error += message;
                return error;
            }

            /** @brief @p type 의 프로퍼티 중 이름이 @p pName 인 것입니다(철자 그대로). */
            static const PropertyInfo* findProperty( const TypeInfo& type, const utf8* pName )
            {
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( prop.matchesNameText( pName ) )
                        return &prop;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool UIStyleSelector::parse( string_view text, UIStyleSelector& outSelector, string& outError )
    {
        outSelector     = UIStyleSelector{};
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
            UIStyleSelectorPart part{};
            if ( UIStyleSheetInternal::parsePart( text.substr( start, position - start ), part, outSelector._specificity, outError ) == false )
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

    bool UIStyleSelector::matchesPart( const UIStyleSelectorPart& part, const Widget& widget, bool bNavigationMode )
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
            if ( UIStyleSheetInternal::hasStyleClass( widget.getStyleClass(), className ) == false )
                return false;
        }
        if ( part._stateMask == 0 )
            return true;
        uint32 states = widget.computeStyleStates();
        if ( bNavigationMode && ( states & UIStyleState::kFocus ) != 0 )
            states |= UIStyleState::kFocusVisible;
        return ( states & part._stateMask ) == part._stateMask;
    }

    bool UIStyleSelector::matches( const Widget& widget, bool bNavigationMode ) const
    {
        if ( _listPart.empty() || matchesPart( _listPart.back(), widget, bNavigationMode ) == false )
            return false;
        const Widget* pAncestor = widget.getParent();
        for ( size_t index = _listPart.size() - 1; index > 0; --index )
        {
            const UIStyleSelectorPart& part = _listPart[index - 1];
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

    bool UIStyleSheetAsset::parse( string_view text, string_view path, UIStyleSheetAsset& outSheet, string& outError )
    {
        using Internal = UIStyleSheetInternal;
        UIStyleSheetAsset sheet{};
        sheet._path = FileUtil::normalizePath( path );
        XMLDocument document;
        // 부모(`_base`)는 데이터 읽기가 펼친다 — 규칙은 `Content/engine/settings/datacataloginheritance.xml` 한 줄이고 합치기 코드는 여기 없다.
        if ( document.parseData( text, path ) == false )
        {
            outError = document.getLastError();
            return false;
        }
        Internal::ParseContext context{ text, path, {}, &sheet, &outError, text.find( DataInheritance::kBaseAttribute ) != string_view::npos };
        const XMLNode          root = document.getRoot();
        if ( root.isValid() == false || StringUtil::equals( root.getName(), UIStyleSheetAsset::kRootElementName ) == false )
        {
            outError = Internal::makeError( path, 1, "the root element must be <UIStyleSheet>" );
            return false;
        }
        bool bVersioned{ false };
        for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( StringUtil::equals( attribute.getName(), sw::kSchemaVersionKey ) == false )
                return context.fail( root, string( "UIStyleSheet has unknown attribute '" ) + attribute.getName() + "'" );
            int32 version{ 0 };
            if ( StringUtil::parseInt( string( attribute.getValue() ), version ) == false || version != static_cast<int32>( UIStyleSheetAsset::kVersion ) )
                return context.fail( root, formatToString( "%#%#' is not supported (expected %#)", string( "_schemaVersion '" ), attribute.getValue(), UIStyleSheetAsset::kVersion ) );
            bVersioned = true;
        }
        if ( bVersioned == false )
            return context.fail( root, "UIStyleSheet needs _schemaVersion" );

        // 변수는 시트 안 어디에 적든 쓸 수 있으므로 먼저 모은다.
        for ( XMLNode child = root.findChild( Internal::kVariableElement ); child.isValid(); child = child.findNextSibling( Internal::kVariableElement ) )
        {
            const utf8* pName  = child.findAttribute( Internal::kNameAttribute );
            const utf8* pValue = child.findAttribute( Internal::kValueAttribute );
            if ( StringUtil::isNullOrEmpty( pName ) || pValue == nullptr )
                return context.fail( child, "Variable needs _name and _value" );
            context._listVariable.push_back( Internal::Variable{ string( pName ), string( pValue ) } );
        }
        for ( XMLNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, Internal::kVariableElement ) || StringUtil::equals( pName, Internal::kVariationElement ) )
                continue;
            if ( StringUtil::equals( pName, Internal::kRuleElement ) == false )
                return context.fail( child, string( "UIStyleSheet has unknown element <" ) + pName + ">" );
            if ( context.parseRule( child ) == false )
                return false;
        }
        // 변형은 시트의 규칙이 다 모인 뒤에 푼다 — 시트 어디에 적든 같다.
        for ( XMLNode child = root.findChild( Internal::kVariationElement ); child.isValid(); child = child.findNextSibling( Internal::kVariationElement ) )
        {
            if ( context.expandTypeVariation( child ) == false )
                return false;
        }
        outSheet = std::move( sheet );
        return true;
    }

    void UIStyleSheetAsset::applyAssignment( const UIStyleAssignment& assignment, WidgetStyle& inoutStyle )
    {
        if ( assignment._pProperty == nullptr )
            return;
        if ( assignment._pNestedProperty == nullptr )
        {
            (void)SerializerUtil::applyPropertyText( *assignment._pProperty, MutableObjectAddress{ &inoutStyle }, assignment._text, SerializeContext::getDefault( SerializePurpose::Authoring ) ); // 읽을 때 이미 확인했다
            return;
        }
        void* pNested = assignment._pProperty->getRawPtr( &inoutStyle );
        (void)SerializerUtil::applyPropertyText( *assignment._pNestedProperty, MutableObjectAddress{ pNested }, assignment._text, SerializeContext::getDefault( SerializePurpose::Authoring ) ); // 읽을 때 이미 확인했다
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( UIStyleSheetAsset, UIStyleSheetAsset::kAssetType );
} // namespace sw
