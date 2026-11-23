#include "pch.h"

#include "Engine/Serialization/Inherit/DataCatalogReferences.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct DataCatalogReferencesInternal
        {
            static constexpr const utf8* kCatalogElement   = "Catalog";    ///< 표: 카탈로그 루트 하나
            static constexpr const utf8* kReferenceElement = "Reference";  ///< 표: 참조 한 줄
            static constexpr const utf8* kUniqueElement    = "Unique";     ///< 표: 한 파일 안에서 겹치면 안 되는 값 한 줄
            static constexpr const utf8* kKeyTargetElement = "CatalogKey"; ///< 표: 항목 종류 한 줄(`CatalogKey<Tag>` 의 Tag → 카탈로그 루트 · 원소)
            static constexpr const utf8* kKeyFieldElement  = "Field";      ///< 표: 카탈로그 키 칸 한 줄(씬 · 프리팹 원소의 칸)
            static constexpr const utf8* kKeyStructElement = "Struct";     ///< 표: 구조체 칸 한 줄(칸 이름으로 적힌 자식 원소의 형식)
            static constexpr const utf8* kKeyTypeElement   = "Element";    ///< 표: 정의 형식 ↔ 카탈로그 원소 한 줄
            static constexpr const utf8* kListItemElement  = "item";       ///< 리플렉션 XML 이 목록 원소를 적는 원소 이름(`<_listX><item>…</item></_listX>`)
            static constexpr const utf8* kDefaultTargetKey = "id";         ///< 가리키는 원소의 id 속성(적지 않으면)

            /** @brief 표 줄 하나의 불리언 속성을 읽습니다. 없으면 false, 틀린 글이면 오류입니다. */
            [[nodiscard]] static bool readFlag( const XMLNode& row, const utf8* pName, string_view sourceName, bool& outValue )
            {
                outValue               = false;
                const string_view text = row.getAttributeText( pName );
                if ( text.empty() || StringUtil::tryParseBool( text, outValue ) )
                    return true;
                SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%#> has %#=\"%#\", which is not a boolean", sourceName, kReferenceElement, pName, text );
                return false;
            }

            /** @brief 표 줄 하나를 규칙으로 읽습니다. 겹침 줄(`<Unique>`)은 owner · attribute 만 받는다. */
            [[nodiscard]] static bool readRule( const XMLNode& row, string_view rootName, string_view sourceName, DataCatalogReferenceRule& outRule )
            {
                const bool                   bUnique           = StringUtil::equals( row.getName(), kUniqueElement );
                static constexpr const utf8* kArrKnown[]       = { "owner", "attribute", "target", "targetKey", "named", "optional", "inEntry", "defaultAttribute", "file", "absent" };
                static constexpr const utf8* kArrUniqueKnown[] = { "owner", "attribute" };
                bool                         bValid            = true;
                for ( XMLAttribute attribute = row.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    const auto matches = [&attribute]( const utf8* pKnown )
                    { return StringUtil::equals( attribute.getName(), pKnown ); };
                    const bool bKnown = bUnique ? std::any_of( std::begin( kArrUniqueKnown ), std::end( kArrUniqueKnown ), matches )
                                                : std::any_of( std::begin( kArrKnown ), std::end( kArrKnown ), matches );
                    if ( bKnown == false )
                    {
                        SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%#> of <%#> has an unknown attribute '%#'", sourceName, row.getName(), rootName, attribute.getName() );
                        bValid = false;
                    }
                }
                outRule._rootName = string( rootName );
                for ( const string_view part : StringUtil::split( row.getAttributeText( "owner" ), "/", { ._bTrim = true, ._bDropEmpty = true } ) )
                {
                    outRule._listOwnerPath.emplace_back( part );
                }
                outRule._attribute          = string( row.getAttributeText( "attribute" ) );
                outRule._target             = string( row.getAttributeText( "target" ) );
                outRule._bUnique            = bUnique;
                const string_view targetKey = row.getAttributeText( "targetKey" );
                outRule._targetKey          = targetKey.empty() ? string( kDefaultTargetKey ) : string( targetKey );
                outRule._defaultAttribute   = string( row.getAttributeText( "defaultAttribute" ) );
                outRule._fileAttribute      = string( row.getAttributeText( "file" ) );
                if ( outRule._listOwnerPath.empty() || outRule._attribute.empty() || ( bUnique == false && outRule._target.empty() ) )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: a <%#> of <%#> needs owner, attribute%#", sourceName, row.getName(), rootName, bUnique ? "" : " and target" );
                    bValid = false;
                }
                bValid = readFlag( row, "named", sourceName, outRule._bNamed ) && bValid;
                bValid = readFlag( row, "optional", sourceName, outRule._bOptional ) && bValid;
                bValid = readFlag( row, "inEntry", sourceName, outRule._bInEntry ) && bValid;
                bValid = readFlag( row, "absent", sourceName, outRule._bAbsent ) && bValid;
                // 지운 id 는 "없음" 을 요구한다 — 없어도 된다(optional)는 뜻을 함께 적을 자리가 없다.
                if ( outRule._bAbsent && outRule._bOptional )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: a <%#> of <%#> has both absent and optional", sourceName, row.getName(), rootName );
                    bValid = false;
                }
                // 다른 파일의 원소는 그 파일 루트의 자식이다 — 같은 항목 안(`inEntry`)과 함께 적을 자리가 없다.
                if ( outRule._bInEntry && outRule._fileAttribute.empty() == false )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: a <%#> of <%#> has both inEntry and file", sourceName, row.getName(), rootName );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief 표 줄 @p row(`<CatalogKey>` · `<Field>`)의 속성이 @p listRequired · @p listOptional 안에 있고 필수 속성이 모두 적혔는지 봅니다. */
            [[nodiscard]] static bool validateKeyRowAttributes( const XMLNode& row, const vector<string_view>& listRequired, const vector<string_view>& listOptional, string_view sourceName )
            {
                bool bValid = true;
                for ( XMLAttribute attribute = row.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    const string_view name = attribute.getName();
                    if ( VectorUtil::contains( listRequired, name ) == false && VectorUtil::contains( listOptional, name ) == false )
                    {
                        SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%#> has an unknown attribute '%#'", sourceName, row.getName(), name );
                        bValid = false;
                    }
                }
                for ( const string_view required : listRequired )
                {
                    if ( row.getAttributeText( string( required ).c_str() ).empty() )
                    {
                        SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%#> needs %#=\"...\"", sourceName, row.getName(), required );
                        bValid = false;
                    }
                }
                return bValid;
            }

            /** @brief 칸 값 글이 빈 키인지 봅니다 — 키는 `hashed_string` 이라 "None" 철자(대소문자 무시)도 빈 이름으로 읽힌다(쌓인 데이터). */
            static bool isEmptyKeyText( string_view value ) { return value.empty() || StringUtil::equals( value, "None", true ); }

            /** @brief 칸 주인 원소 @p owner 의 칸 @p field 값(속성 하나, 또는 같은 이름 자식 원소의 `<item>` 글)을 @p visit 에 넘깁니다. 빈 키는 적지 않은 것이다. */
            template <typename TVisit>
            static void visitFieldValues( const XMLNode& owner, const string& field, const TVisit& visit )
            {
                const string_view attributeValue = StringUtil::trim( owner.getAttributeText( field.c_str() ) );
                if ( isEmptyKeyText( attributeValue ) == false )
                    visit( attributeValue );
                for ( XMLNode list = owner.findChild( field.c_str() ); list.isValid(); list = list.findNextSibling( field.c_str() ) )
                {
                    for ( XMLNode item = list.findChild( kListItemElement ); item.isValid(); item = item.findNextSibling( kListItemElement ) )
                    {
                        const string_view value = StringUtil::trim( string_view( item.getText() ) );
                        if ( isEmptyKeyText( value ) == false )
                            visit( value );
                    }
                }
            }

            /** @brief 값 글 @p value 의 id 들을 @p visit 에 넘깁니다(목록을 나누고, `id:수` 꼴이면 콜론 앞만). */
            template <typename TVisit>
            static void visitIDs( string_view value, bool bNamed, const TVisit& visit )
            {
                for ( string_view token : StringUtil::split( value, XMLReadUtil::kListSeparators, { ._bTrim = true, ._bDropEmpty = true } ) )
                {
                    if ( bNamed )
                    {
                        const size_t colon = token.find( ':' );
                        if ( colon != string_view::npos )
                            token = StringUtil::trim( token.substr( 0, colon ) );
                    }
                    if ( token.empty() == false )
                        visit( token );
                }
            }

            /** @brief @p parent 의 자식 중 규칙의 대상 원소의 id 를 @p outListID 에 정렬해 담습니다(먼저 비운다). */
            static void collectIDs( const XMLNode& parent, const DataCatalogReferenceRule& rule, vector<string_view>& outListID )
            {
                outListID.clear();
                for ( XMLNode target = parent.findChild( rule._target.c_str() ); target.isValid(); target = target.findNextSibling( rule._target.c_str() ) )
                {
                    const string_view id = target.getAttributeText( rule._targetKey.c_str() );
                    if ( id.empty() == false )
                        outListID.push_back( id );
                }
                VectorUtil::sort( outListID );
            }

            /** @brief @p node 아래에서 경로 @p listPath 의 @p depth 번째 원소부터 따라가 끝 원소마다 @p visit 를 부릅니다. */
            template <typename TVisit>
            static void visitOwners( const XMLNode& node, const vector<string>& listPath, size_t depth, const TVisit& visit )
            {
                for ( XMLNode child = node.findChild( listPath[depth].c_str() ); child.isValid(); child = child.findNextSibling( listPath[depth].c_str() ) )
                {
                    if ( depth + 1 == listPath.size() )
                        visit( child );
                    else
                        visitOwners( child, listPath, depth + 1, visit );
                }
            }

            /** @brief 글 @p text 에 원소 `<name` 이 적혔으면 true 입니다(뒤가 이름의 끝인 자리만). */
            static bool isElementMentioned( string_view text, const string& name )
            {
                StringBuilder<constant::buffer::kMax128> openingBuilder;
                openingBuilder.appendFormat( "<%#", name );
                const string_view opening = openingBuilder.view();
                for ( size_t at = text.find( opening ); at != string_view::npos; at = text.find( opening, at + 1 ) )
                {
                    const size_t after = at + opening.size();
                    if ( after >= text.size() )
                        return false;
                    const utf8 next = text[after];
                    if ( next == '>' || next == '/' || next == ' ' || next == '\t' || next == '\r' || next == '\n' )
                        return true;
                }
                return false;
            }
        };
    } // namespace

    DataCatalogReferenceRules::DataCatalogReferenceRules()
        : _listRule{}
        , _listKeyTarget{}
        , _listKeyField{}
        , _listKeyStruct{}
        , _listKeyElement{}
    {
    }

    bool DataCatalogReferenceRules::loadFromAsset()
    {
        string text;
        if ( AssetIO::readTextAsset( AssetID{ kTableAssetID }, text ) == false )
        {
            SW_LOG_ERROR_ID( "DataCatalogReferences.TableUnreadable", "Catalog reference rule table '%#' could not be read", kTableAssetID );
            return false;
        }
        return loadFromXMLText( text, kTableAssetID );
    }

    bool DataCatalogReferenceRules::loadFromXMLText( string_view tableText, string_view sourceName )
    {
        using Internal = DataCatalogReferencesInternal;
        _listRule.clear();
        _listKeyTarget.clear();
        _listKeyField.clear();
        _listKeyStruct.clear();
        _listKeyElement.clear();
        XMLDocument doc;
        if ( doc.parse( tableText, sourceName ) == false || doc.getRoot().isValid() == false )
        {
            SW_LOG_ERROR_ID( "DataCatalogReferences.TableInvalid", "Catalog reference rule table '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return false;
        }
        const vector<string_view> listKeyTargetRequired{ "tag", "catalog", "entry" };
        const vector<string_view> listKeyTargetOptional{ "targetKey" };
        const vector<string_view> listKeyFieldRequired{ "owner", "field", "tag" };
        const vector<string_view> listKeyFieldOptional{ "attribute" };
        const vector<string_view> listKeyStructRequired{ "owner", "field", "type" };
        const vector<string_view> listKeyTypeRequired{ "type", "catalog", "path" };
        bool                      bValid = true;
        for ( XMLNode catalog = doc.getRoot().findChild(); catalog.isValid(); catalog = catalog.findNextSibling() )
        {
            if ( StringUtil::equals( catalog.getName(), Internal::kKeyTargetElement ) )
            {
                if ( Internal::validateKeyRowAttributes( catalog, listKeyTargetRequired, listKeyTargetOptional, sourceName ) == false )
                {
                    bValid = false;
                    continue;
                }
                const string_view    targetKey = catalog.getAttributeText( "targetKey" );
                DataCatalogKeyTarget target{ string( catalog.getAttributeText( "tag" ) ), string( catalog.getAttributeText( "catalog" ) ), string( catalog.getAttributeText( "entry" ) ),
                                             targetKey.empty() ? string( Internal::kDefaultTargetKey ) : string( targetKey ) };
                if ( findKeyTarget( target._tag ) != nullptr )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%# tag=\"%#\"> is written twice", sourceName, Internal::kKeyTargetElement, target._tag );
                    bValid = false;
                    continue;
                }
                _listKeyTarget.push_back( std::move( target ) );
                continue;
            }
            if ( StringUtil::equals( catalog.getName(), Internal::kKeyFieldElement ) )
            {
                if ( Internal::validateKeyRowAttributes( catalog, listKeyFieldRequired, listKeyFieldOptional, sourceName ) == false )
                {
                    bValid = false;
                    continue;
                }
                DataCatalogKeyField field{ string( catalog.getAttributeText( "owner" ) ), string( catalog.getAttributeText( "field" ) ), string( catalog.getAttributeText( "tag" ) ),
                                           string( catalog.getAttributeText( "attribute" ) ) };
                if ( findKeyField( field._owner, field._field ) != nullptr )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%# owner=\"%#\" field=\"%#\"> is written twice", sourceName, Internal::kKeyFieldElement, field._owner, field._field );
                    bValid = false;
                    continue;
                }
                _listKeyField.push_back( std::move( field ) );
                continue;
            }
            if ( StringUtil::equals( catalog.getName(), Internal::kKeyStructElement ) )
            {
                if ( Internal::validateKeyRowAttributes( catalog, listKeyStructRequired, {}, sourceName ) == false )
                {
                    bValid = false;
                    continue;
                }
                DataCatalogKeyStruct row{ string( catalog.getAttributeText( "owner" ) ), string( catalog.getAttributeText( "field" ) ), string( catalog.getAttributeText( "type" ) ) };
                if ( findKeyStruct( row._owner, row._field ) != nullptr )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%# owner=\"%#\" field=\"%#\"> is written twice", sourceName, Internal::kKeyStructElement, row._owner, row._field );
                    bValid = false;
                    continue;
                }
                _listKeyStruct.push_back( std::move( row ) );
                continue;
            }
            if ( StringUtil::equals( catalog.getName(), Internal::kKeyTypeElement ) )
            {
                if ( Internal::validateKeyRowAttributes( catalog, listKeyTypeRequired, {}, sourceName ) == false )
                {
                    bValid = false;
                    continue;
                }
                DataCatalogKeyElement row{ string( catalog.getAttributeText( "type" ) ), string( catalog.getAttributeText( "catalog" ) ), string( catalog.getAttributeText( "path" ) ) };
                const bool            bRepeated = VectorUtil::containsIf( _listKeyElement, [&row]( const DataCatalogKeyElement& other )
                           { return other._type == row._type && other._catalog == row._catalog && other._path == row._path; } ); // 한 정의가 여러 자리에 놓이면 줄도 여럿이다(메크 무기)
                if ( bRepeated )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%# type=\"%#\" path=\"%#\"> is written twice", sourceName, Internal::kKeyTypeElement, row._type, row._path );
                    bValid = false;
                    continue;
                }
                _listKeyElement.push_back( std::move( row ) );
                continue;
            }
            const string_view rootName = catalog.getAttributeText( "root" );
            if ( StringUtil::equals( catalog.getName(), Internal::kCatalogElement ) == false || rootName.empty() )
            {
                SW_LOG_ERROR_ID( "DataCatalogReferences.TableInvalid2", "%#: <%#> is not a <%# root=\"...\">, <%#>, <%#>, <%#> or <%#> row", sourceName, catalog.getName(),
                                 Internal::kCatalogElement, Internal::kKeyTargetElement, Internal::kKeyFieldElement, Internal::kKeyStructElement, Internal::kKeyTypeElement );
                bValid = false;
                continue;
            }
            for ( XMLNode row = catalog.findChild(); row.isValid(); row = row.findNextSibling() )
            {
                if ( StringUtil::equals( row.getName(), Internal::kReferenceElement ) == false && StringUtil::equals( row.getName(), Internal::kUniqueElement ) == false )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%#> of <%#> is not a <%#> or <%#> row", sourceName, row.getName(), rootName, Internal::kReferenceElement,
                                     Internal::kUniqueElement );
                    bValid = false;
                    continue;
                }
                DataCatalogReferenceRule rule;
                if ( Internal::readRule( row, rootName, sourceName, rule ) )
                    _listRule.push_back( std::move( rule ) );
                else
                    bValid = false;
            }
        }
        // 칸 줄의 종류는 표에 있어야 한다 — 없으면 쿠커가 그 칸을 어느 카탈로그와 대조할지 모른다.
        for ( const DataCatalogKeyField& field : _listKeyField )
        {
            if ( findKeyTarget( field._tag ) == nullptr )
            {
                SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: <%# owner=\"%#\" field=\"%#\"> names tag '%#', which has no <%#> row", sourceName, Internal::kKeyFieldElement, field._owner,
                                 field._field, field._tag, Internal::kKeyTargetElement );
                bValid = false;
            }
        }
        if ( bValid == false )
        {
            _listRule.clear(); // 깨진 표로 반만 검사하지 않는다
            _listKeyTarget.clear();
            _listKeyField.clear();
            _listKeyStruct.clear();
            _listKeyElement.clear();
        }
        return bValid;
    }

    const DataCatalogKeyStruct* DataCatalogReferenceRules::findKeyStruct( string_view owner, string_view field ) const
    {
        return VectorUtil::findIf( _listKeyStruct, [owner, field]( const DataCatalogKeyStruct& row )
        { return row._owner == owner && row._field == field; } );
    }

    const DataCatalogKeyElement* DataCatalogReferenceRules::findKeyElement( string_view type ) const
    {
        return VectorUtil::findIf( _listKeyElement, [type]( const DataCatalogKeyElement& row )
        { return row._type == type; } );
    }

    string DataCatalogReferenceRules::findElementAttribute( const DataCatalogKeyField& field )
    {
        if ( field._attribute.empty() == false )
            return field._attribute;
        // `XMLFields::getAttributeName` 과 같은 규칙(키 칸은 불리언이 아니라 `_` 만 뗀다) — 쿠커는 리플렉션을 못 보므로 철자를 여기 둔다. 어긋나면 FieldTableMatchesReflection 이 진다.
        return StringUtil::startsWith( field._field, "_" ) ? field._field.substr( 1 ) : field._field;
    }

    void DataCatalogReferenceRules::collectEntryIDs( const XMLNode& root, const DataCatalogKeyTarget& target, vector<string_view>& inoutListID )
    {
        vector<string> listPath;
        for ( const string_view part : StringUtil::split( target._entry, "/", { ._bTrim = true, ._bDropEmpty = true } ) )
        {
            listPath.emplace_back( part );
        }
        if ( listPath.empty() )
            return;
        // 항목 하나짜리 파일(루트가 항목 원소 — 에디터가 내보낸 `*.appearance.xml`, 런타임 `AppearanceDatabase::loadPresetFile` 이 읽는 꼴)도 그 항목을 정의한다.
        // 카탈로그 루트만 보면 에디터 콤보 · 쿠커 키 검사가 런타임이 읽는 항목을 모른다.
        if ( listPath.size() == 1 && listPath[0] == string_view( root.getName() ) )
        {
            const string_view id = root.getAttributeText( target._targetKey.c_str() );
            if ( id.empty() == false )
                inoutListID.push_back( id );
            return;
        }
        if ( target._catalog != string_view( root.getName() ) )
            return;
        DataCatalogReferencesInternal::visitOwners( root, listPath, 0, [&]( const XMLNode& entry )
        {
            const string_view id = entry.getAttributeText( target._targetKey.c_str() );
            if ( id.empty() == false )
                inoutListID.push_back( id );
        } );
    }

    const DataCatalogKeyTarget* DataCatalogReferenceRules::findKeyTarget( string_view tag ) const
    {
        return VectorUtil::findIf( _listKeyTarget, [tag]( const DataCatalogKeyTarget& target )
        { return target._tag == tag; } );
    }

    const DataCatalogKeyField* DataCatalogReferenceRules::findKeyField( string_view owner, string_view field ) const
    {
        return VectorUtil::findIf( _listKeyField, [owner, field]( const DataCatalogKeyField& row )
        { return row._owner == owner && row._field == field; } );
    }

    bool DataCatalogReferenceRules::hasRulesFor( string_view rootName ) const
    {
        return VectorUtil::containsIf( _listRule, [rootName]( const DataCatalogReferenceRule& rule )
        { return rule._rootName == rootName; } );
    }

    bool DataCatalogReferenceRules::isRootMentioned( string_view text ) const
    {
        for ( const DataCatalogReferenceRule& rule : _listRule )
        {
            if ( DataCatalogReferencesInternal::isElementMentioned( text, rule._rootName ) )
                return true;
        }
        return false;
    }

    bool DataCatalogReferenceRules::validateDocument( const XMLNode& root, string_view sourceName ) const
    {
        using Internal             = DataCatalogReferencesInternal;
        const string_view rootName = root.getName();
        bool              bValid   = true;
        for ( const DataCatalogReferenceRule& rule : _listRule )
        {
            if ( rule._rootName != rootName )
                continue;
            if ( rule._bUnique )
            {
                bValid = validateUnique( root, rule, sourceName ) && bValid;
                continue;
            }
            // 가리킬 수 있는 id — 정렬해 두고 이진 검색한다(항목 수십 · 수백). 항목 안 참조(`inEntry`)는 항목마다 다시 모은다.
            // 다른 파일의 원소(`file`)는 루트가 적은 리소스를 펼쳐 읽어 그 루트에서 모은다 — 루트가 적지 않았으면 이 줄은 보지 않는다(파일 이름이 필수인지는 읽는 코드가 안다).
            vector<string_view> listTargetID;
            XMLDocument         targetDoc;
            string_view         targetFile;
            if ( rule._fileAttribute.empty() == false )
            {
                targetFile = root.getAttributeText( rule._fileAttribute.c_str() );
                if ( targetFile.empty() )
                    continue;
                if ( targetDoc.loadResource( AssetID{ targetFile } ) == false || targetDoc.getRoot().isValid() == false )
                {
                    SW_LOG_ERROR_ID( "XMLCatalog.ReferenceFileUnreadable", "%#: %#=\"%#\" could not be read - <%#> references cannot be checked and the catalog does not load", sourceName,
                                     rule._fileAttribute.c_str(), targetFile, rule._target.c_str() );
                    bValid = false;
                    continue;
                }
                Internal::collectIDs( targetDoc.getRoot(), rule, listTargetID );
            }
            else if ( rule._bInEntry == false )
                Internal::collectIDs( root, rule, listTargetID );
            for ( XMLNode entry = root.findChild( rule._listOwnerPath[0].c_str() ); entry.isValid(); entry = entry.findNextSibling( rule._listOwnerPath[0].c_str() ) )
            {
                if ( rule._bInEntry )
                    Internal::collectIDs( entry, rule, listTargetID );
                const string_view entryID      = entry.getAttributeText( "id" );
                const string_view ownerName    = entryID.empty() ? string_view{ entry.getName() } : entryID;
                const auto        validateNode = [&]( const XMLNode& owner )
                {
                    string_view value = owner.getAttributeText( rule._attribute.c_str() );
                    if ( value.empty() && rule._defaultAttribute.empty() == false )
                        value = owner.getAttributeText( rule._defaultAttribute.c_str() );
                    Internal::visitIDs( value, rule._bNamed, [&]( string_view id )
                    {
                        const bool bFound = std::binary_search( listTargetID.begin(), listTargetID.end(), id );
                        if ( rule._bAbsent )
                        {
                            if ( bFound == false )
                                return; // 지운 id 다 — 그래야 한다
                            SW_LOG_ERROR_ID( "XMLCatalog.RemovedIDStillPresent", "%#: <%#> '%#' %#=\"%#\" must name a removed <%#>, but this catalog still has it - the catalog does not load", sourceName,
                                             owner.getName(), ownerName, rule._attribute.c_str(), id, rule._target.c_str() );
                            bValid = false;
                            return;
                        }
                        if ( bFound )
                            return;
                        if ( rule._bOptional )
                        {
                            SW_LOG_WARNING_ID( "XMLCatalog.UnknownOptionalReference", "%#: <%#> '%#' %#=\"%#\" names no <%#> in this file - the optional link is left out", sourceName,
                                               owner.getName(), ownerName, rule._attribute.c_str(), id, rule._target.c_str() );
                            return;
                        }
                        SW_LOG_ERROR_ID( "XMLCatalog.UnknownReference", "%#: <%#> '%#' %#=\"%#\" names an unknown <%#>%#%# - the catalog does not load", sourceName, owner.getName(), ownerName,
                                         rule._attribute.c_str(), id, rule._target.c_str(), targetFile.empty() ? "" : " in ", targetFile );
                        bValid = false;
                    } );
                };
                if ( rule._listOwnerPath.size() == 1 )
                    validateNode( entry );
                else
                    Internal::visitOwners( entry, rule._listOwnerPath, 1, validateNode );
            }
        }
        return bValid;
    }

    bool DataCatalogReferenceRules::validateUnique( const XMLNode& root, const DataCatalogReferenceRule& rule, string_view sourceName )
    {
        unordered_set<string_view> uniqueSeen; // 줄마다 본 값 목록을 훑지 않는다(카탈로그 줄 수의 제곱). 값은 문서가 든 글이다
        bool                       bValid       = true;
        const auto                 validateNode = [&]( const XMLNode& owner )
        {
            const string_view value = owner.getAttributeText( rule._attribute.c_str() );
            if ( value.empty() )
                return; // 빈 값은 그 원소의 읽기가 오류로 알린다
            if ( uniqueSeen.insert( value ).second == false )
            {
                SW_LOG_ERROR_ID( "XMLCatalog.DuplicateID", "%#: <%#> %#=\"%#\" is written twice in one file - the catalog does not load", sourceName, owner.getName(), rule._attribute.c_str(), value );
                bValid = false;
            }
        };
        for ( XMLNode entry = root.findChild( rule._listOwnerPath[0].c_str() ); entry.isValid(); entry = entry.findNextSibling( rule._listOwnerPath[0].c_str() ) )
        {
            if ( rule._listOwnerPath.size() == 1 )
                validateNode( entry );
            else
                DataCatalogReferencesInternal::visitOwners( entry, rule._listOwnerPath, 1, validateNode );
        }
        return bValid;
    }

    bool DataCatalogReferenceRules::validateWithTable( const XMLNode& root, string_view sourceName )
    {
        DataCatalogReferenceRules rules;
        return rules.loadFromAsset() && rules.validateDocument( root, sourceName );
    }

    // ------------------------------------------------------------------------------
    // DataCatalogKeyCheck
    // ------------------------------------------------------------------------------
    DataCatalogKeyCheck::DataCatalogKeyCheck( const DataCatalogReferenceRules& rules )
        : _rules{ rules }
        , _mapTagID{}
        , _listUse{}
    {
    }

    bool DataCatalogKeyCheck::isMentioned( string_view text ) const
    {
        for ( const DataCatalogKeyTarget& target : _rules.getKeyTargets() )
        {
            if ( DataCatalogReferencesInternal::isElementMentioned( text, target._catalog ) )
                return true;
        }
        for ( const DataCatalogKeyField& field : _rules.getFieldRows() )
        {
            if ( DataCatalogReferencesInternal::isElementMentioned( text, field._owner ) )
                return true;
        }
        for ( const DataCatalogKeyStruct& row : _rules.getStructRows() )
        {
            if ( DataCatalogReferencesInternal::isElementMentioned( text, row._owner ) )
                return true;
        }
        for ( const DataCatalogKeyElement& row : _rules.getElementRows() )
        {
            if ( DataCatalogReferencesInternal::isElementMentioned( text, row._catalog ) )
                return true;
        }
        return false;
    }

    void DataCatalogKeyCheck::addDocument( const XMLNode& root, string_view sourceName )
    {
        const string_view   rootName = root.getName();
        vector<string_view> listID;
        for ( const DataCatalogKeyTarget& target : _rules.getKeyTargets() )
        {
            listID.clear();
            DataCatalogReferenceRules::collectEntryIDs( root, target, listID );
            unordered_set<string>& setID = _mapTagID[target._tag];
            for ( const string_view id : listID )
            {
                setID.insert( string( id ) );
            }
        }
        // 카탈로그 정의의 키 칸(무기 → 탄)은 정의 원소(`<Element>`)에서 속성으로 찾는다.
        for ( const DataCatalogKeyElement& element : _rules.getElementRows() )
        {
            if ( element._catalog != rootName )
                continue;
            vector<string> listPath;
            for ( const string_view part : StringUtil::split( element._path, "/", { ._bTrim = true, ._bDropEmpty = true } ) )
            {
                listPath.emplace_back( part );
            }
            if ( listPath.empty() == false )
                DataCatalogReferencesInternal::visitOwners( root, listPath, 0, [&]( const XMLNode& node )
                { collectTypedUses( node, element._type, true, sourceName ); } );
        }
        collectUses( root, sourceName );
    }

    void DataCatalogKeyCheck::collectUses( const XMLNode& node, string_view sourceName )
    {
        collectTypedUses( node, node.getName(), false, sourceName );
        for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( child.isElement() )
                collectUses( child, sourceName );
        }
    }

    void DataCatalogKeyCheck::collectTypedUses( const XMLNode& node, string_view typeName, bool bCatalogElement, string_view sourceName )
    {
        using Internal = DataCatalogReferencesInternal;
        for ( const DataCatalogKeyField& field : _rules.getFieldRows() )
        {
            if ( field._owner != typeName )
                continue;
            const auto addUse = [&]( string_view id )
            {
                if ( Internal::isEmptyKeyText( id ) == false )
                    _listUse.push_back( KeyUse{ field._tag, string( id ), string( sourceName ), field._owner, field._field } );
            };
            if ( bCatalogElement )
            {
                const string attribute = DataCatalogReferenceRules::findElementAttribute( field );
                Internal::visitIDs( node.getAttributeText( attribute.c_str() ), false, addUse ); // `XMLFields` 목록은 쉼표 목록이다
            }
            else
                Internal::visitFieldValues( node, field._field, addUse );
        }
        // 구조체 칸 하나는 칸 이름의 원소로 적힌다 — 그 원소를 칸 형식으로 다시 본다(일반 순회는 원소 이름이 칸 이름이라 주인을 못 찾는다).
        if ( bCatalogElement )
            return;
        for ( const DataCatalogKeyStruct& row : _rules.getStructRows() )
        {
            if ( row._owner != typeName )
                continue;
            for ( XMLNode child = node.findChild( row._field.c_str() ); child.isValid(); child = child.findNextSibling( row._field.c_str() ) )
            {
                collectTypedUses( child, row._type, false, sourceName );
            }
        }
    }

    bool DataCatalogKeyCheck::validate() const
    {
        bool bValid = true;
        for ( const KeyUse& use : _listUse )
        {
            const auto mapIter = _mapTagID.find( use._tag );
            if ( mapIter != _mapTagID.end() && mapIter->second.contains( use._id ) )
                continue;
            const DataCatalogKeyTarget* pTarget = _rules.findKeyTarget( use._tag );
            SW_LOG_ERROR_ID( "DataCatalogKey.UnknownEntry", "%#: <%#> %#=\"%#\" names no <%#> in any <%#> being cooked (rules in %#)", use._sourceName, use._owner, use._field, use._id,
                             pTarget != nullptr ? pTarget->_entry.c_str() : use._tag.c_str(), pTarget != nullptr ? pTarget->_catalog.c_str() : "?", DataCatalogReferenceRules::kTableAssetID );
            bValid = false;
        }
        return bValid;
    }
} // namespace sw

// 표는 코드가 이름으로 읽는다 — 배포 런타임의 카탈로그 읽기도 같은 검사를 하므로 엔진 팩에 담는다.
SW_ASSET_COOK_ROOT( DataCatalogReferenceTable, ::sw::DataCatalogReferenceRules::kTableAssetID );
