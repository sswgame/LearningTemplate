#include "pch.h"

#include "Engine/Serialization/Inherit/DataCatalogReferences.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct DataCatalogReferencesInternal
        {
            static constexpr const utf8* kCatalogElement   = "Catalog";   ///< 표: 카탈로그 루트 하나
            static constexpr const utf8* kReferenceElement = "Reference"; ///< 표: 참조 한 줄
            static constexpr const utf8* kUniqueElement    = "Unique";    ///< 표: 한 파일 안에서 겹치면 안 되는 값 한 줄
            static constexpr const utf8* kDefaultTargetKey = "id";        ///< 가리키는 원소의 id 속성(적지 않으면)
            static constexpr const utf8* kListSeparators   = ",; \t\r\n"; ///< 목록 값의 구분자 — 카탈로그 읽기(`XMLFields` · `forEachNamedNumber`)가 받는 것과 같다

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
                static constexpr const utf8* kArrKnown[]       = { "owner", "attribute", "target", "targetKey", "named", "optional", "inEntry" };
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
                if ( outRule._listOwnerPath.empty() || outRule._attribute.empty() || ( bUnique == false && outRule._target.empty() ) )
                {
                    SW_LOG_ERROR_ID( "DataCatalogReferences.RuleInvalid", "%#: a <%#> of <%#> needs owner, attribute%#", sourceName, row.getName(), rootName, bUnique ? "" : " and target" );
                    bValid = false;
                }
                bValid = readFlag( row, "named", sourceName, outRule._bNamed ) && bValid;
                bValid = readFlag( row, "optional", sourceName, outRule._bOptional ) && bValid;
                bValid = readFlag( row, "inEntry", sourceName, outRule._bInEntry ) && bValid;
                return bValid;
            }

            /** @brief 값 글 @p value 의 id 들을 @p visit 에 넘깁니다(목록을 나누고, `id:수` 꼴이면 콜론 앞만). */
            template <typename TVisit>
            static void visitIDs( string_view value, bool bNamed, const TVisit& visit )
            {
                for ( string_view token : StringUtil::split( value, kListSeparators, { ._bTrim = true, ._bDropEmpty = true } ) )
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
                std::sort( outListID.begin(), outListID.end() );
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
        };
    } // namespace

    DataCatalogReferenceRules::DataCatalogReferenceRules()
        : _listRule{}
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
        XMLDocument doc;
        if ( doc.parse( tableText, sourceName ) == false || doc.getRoot().isValid() == false )
        {
            SW_LOG_ERROR_ID( "DataCatalogReferences.TableInvalid", "Catalog reference rule table '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return false;
        }
        bool bValid = true;
        for ( XMLNode catalog = doc.getRoot().findChild(); catalog.isValid(); catalog = catalog.findNextSibling() )
        {
            const string_view rootName = catalog.getAttributeText( "root" );
            if ( StringUtil::equals( catalog.getName(), Internal::kCatalogElement ) == false || rootName.empty() )
            {
                SW_LOG_ERROR_ID( "DataCatalogReferences.TableInvalid2", "%#: <%#> is not a <%# root=\"...\"> row", sourceName, catalog.getName(), Internal::kCatalogElement );
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
        if ( bValid == false )
            _listRule.clear(); // 깨진 표로 반만 검사하지 않는다
        return bValid;
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
            const string opening = "<" + rule._rootName;
            for ( size_t at = text.find( opening ); at != string_view::npos; at = text.find( opening, at + 1 ) )
            {
                const size_t after = at + opening.size();
                if ( after >= text.size() )
                    break;
                const utf8 next = text[after];
                if ( next == '>' || next == '/' || next == ' ' || next == '\t' || next == '\r' || next == '\n' )
                    return true;
            }
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
            vector<string_view> listTargetID;
            if ( rule._bInEntry == false )
                Internal::collectIDs( root, rule, listTargetID );
            for ( XMLNode entry = root.findChild( rule._listOwnerPath[0].c_str() ); entry.isValid(); entry = entry.findNextSibling( rule._listOwnerPath[0].c_str() ) )
            {
                if ( rule._bInEntry )
                    Internal::collectIDs( entry, rule, listTargetID );
                const string_view entryID   = entry.getAttributeText( "id" );
                const string_view ownerName = entryID.empty() ? string_view{ entry.getName() } : entryID;
                const auto        checkNode = [&]( const XMLNode& owner )
                {
                    Internal::visitIDs( owner.getAttributeText( rule._attribute.c_str() ), rule._bNamed, [&]( string_view id )
                    {
                        if ( std::binary_search( listTargetID.begin(), listTargetID.end(), id ) )
                            return;
                        if ( rule._bOptional )
                        {
                            SW_LOG_WARNING_ID( "XMLCatalog.UnknownOptionalReference", "%#: <%#> '%#' %#=\"%#\" names no <%#> in this file - the optional link is left out", sourceName,
                                               owner.getName(), ownerName, rule._attribute.c_str(), id, rule._target.c_str() );
                            return;
                        }
                        SW_LOG_ERROR_ID( "XMLCatalog.UnknownReference", "%#: <%#> '%#' %#=\"%#\" names an unknown <%#> - the catalog does not load", sourceName, owner.getName(), ownerName,
                                         rule._attribute.c_str(), id, rule._target.c_str() );
                        bValid = false;
                    } );
                };
                if ( rule._listOwnerPath.size() == 1 )
                    checkNode( entry );
                else
                    Internal::visitOwners( entry, rule._listOwnerPath, 1, checkNode );
            }
        }
        return bValid;
    }

    bool DataCatalogReferenceRules::validateUnique( const XMLNode& root, const DataCatalogReferenceRule& rule, string_view sourceName )
    {
        unordered_set<string_view> uniqueSeen; // 줄마다 본 값 목록을 훑지 않는다(카탈로그 줄 수의 제곱). 값은 문서가 든 글이다
        bool                       bValid    = true;
        const auto                 checkNode = [&]( const XMLNode& owner )
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
                checkNode( entry );
            else
                DataCatalogReferencesInternal::visitOwners( entry, rule._listOwnerPath, 1, checkNode );
        }
        return bValid;
    }

    bool DataCatalogReferenceRules::validateWithTable( const XMLNode& root, string_view sourceName )
    {
        DataCatalogReferenceRules rules;
        return rules.loadFromAsset() && rules.validateDocument( root, sourceName );
    }
} // namespace sw

// 표는 코드가 이름으로 읽는다 — 배포 런타임의 카탈로그 읽기도 같은 검사를 하므로 엔진 팩에 담는다.
SW_ASSET_COOK_ROOT( DataCatalogReferenceTable, ::sw::DataCatalogReferenceRules::kTableAssetID );
