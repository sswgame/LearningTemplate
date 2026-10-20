#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/HorrorCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/EnumNameTable.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "HorrorCatalog" );

    namespace
    {
        struct HorrorCatalogInternal
        {
            static constexpr const utf8*                   kArrItemKindName[] = { "None", "Weapon", "Ammo", "Heal", "Key", "SaveItem", "Battery", "Document" };
            static constexpr EnumNameTable<HorrorItemKind> kItemKindTable{ kArrItemKindName };
            static_assert( kItemKindTable.getCount() == static_cast<size_t>( HorrorItemKind::Document ) + 1, "item kind names must cover every HorrorItemKind" );

            /** @brief `code="3 1 4"` 를 숫자 목록으로 읽습니다. 비었거나 깨진 숫자가 있으면 오류입니다. */
            [[nodiscard]] static bool readDigits( string_view text, vector<int32>& outListDigit, string_view sourceName, const hashed_string& id )
            {
                outListDigit.clear();
                bool bValid = true;
                StringUtil::forEachToken( text, ",; ", [&]( string_view token )
                {
                    int32 digit = 0;
                    if ( StringUtil::parseInt( token, digit ) )
                        outListDigit.push_back( digit );
                    else
                    {
                        SW_LOG_ERROR( "%#: dial lock '%#' has a bad digit '%#'", sourceName, id.c_str(), token );
                        bValid = false;
                    }
                } );
                if ( outListDigit.empty() )
                {
                    SW_LOG_ERROR( "%#: dial lock '%#' needs code=\"...\"", sourceName, id.c_str() );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief "a-b,c-d" 를 연결 목록으로 읽습니다. 모양이 틀린 연결은 오류입니다. */
            [[nodiscard]] static bool readLinks( string_view text, vector<HorrorClueLink>& outListLink, string_view sourceName, const hashed_string& id )
            {
                outListLink.clear();
                bool bValid = true;
                StringUtil::forEachToken( text, ",; ", [&]( string_view token )
                {
                    const size_t dash           = token.find( '-' );
                    const bool   bMalformedLink = dash == string_view::npos || dash == 0 || dash + 1 >= token.size();
                    if ( bMalformedLink )
                    {
                        SW_LOG_ERROR( "%#: deduction '%#' has a bad link '%#' (expected clueA-clueB)", sourceName, id.c_str(), token );
                        bValid = false;
                        return;
                    }
                    HorrorClueLink link;
                    link._first  = hashed_string( token.substr( 0, dash ) );
                    link._second = hashed_string( token.substr( dash + 1 ) );
                    outListLink.push_back( link );
                } );
                return bValid;
            }
        };
    } // namespace

    bool parseHorrorItemKind( string_view text, HorrorItemKind& outKind )
    {
        return HorrorCatalogInternal::kItemKindTable.parse( text, outKind );
    }

    const utf8* toString( HorrorItemKind kind )
    {
        return HorrorCatalogInternal::kItemKindTable.toString( kind );
    }

    HorrorCatalog::HorrorCatalog()
        : _itemCatalog{}
        , _monsterCatalog{}
        , _keyLockCatalog{}
        , _dialLockCatalog{}
        , _sequenceCatalog{}
        , _documentCatalog{}
        , _deductionCatalog{}
        , _recipeCatalog{}
        , _rules{}
    {
    }

    const RecipeDef* HorrorCatalog::findCombine( const hashed_string& firstItem, const hashed_string& secondItem ) const
    {
        const hashed_string station = getCombineStation();
        for ( const RecipeDef& recipe : _recipeCatalog.getAll() )
        {
            if ( recipe._station != station || recipe._inputs.getTotalCount() != 2 )
                continue;
            if ( firstItem == secondItem )
            {
                if ( recipe._inputs.getItemCount( firstItem ) == 2 )
                    return &recipe;
                continue;
            }
            if ( recipe._inputs.getItemCount( firstItem ) == 1 && recipe._inputs.getItemCount( secondItem ) == 1 )
                return &recipe;
        }
        return nullptr;
    }

    uint32 HorrorCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }
        // 열쇠 자물쇠가 가리키는 열쇠는 모두 읽은 뒤에 확인한다(자물쇠가 아이템보다 앞에 적혀도 된다).
        for ( const HorrorKeyLockDef& lock : _keyLockCatalog.getAll() )
        {
            if ( _itemCatalog.find( lock._keyItem ) == nullptr )
                SW_LOG_ERROR( "%#: key lock '%#' names an unknown key item '%#'", sourceName, lock._id.c_str(), lock._keyItem.c_str() );
        }
        return loadedCount;
    }

    bool HorrorCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        const auto is = [name]( const utf8* pName )
        { return StringUtil::equals( name, string_view( pName ), true ); };
        if ( is( "Rules" ) )
            return GameDataXML::readSetting( node, sourceName, _rules );
        if ( is( "Item" ) )
            return GameDataXML::readInto( node, sourceName, _itemCatalog );
        if ( is( "Combine" ) )
            return loadCombine( node, sourceName );
        if ( is( "Monster" ) )
            return GameDataXML::readInto( node, sourceName, _monsterCatalog );
        if ( is( "KeyLock" ) )
            return GameDataXML::readInto( node, sourceName, _keyLockCatalog );
        if ( is( "DialLock" ) )
            return loadDialLock( node, sourceName );
        if ( is( "Sequence" ) )
            return loadSequence( node, sourceName );
        if ( is( "Document" ) )
            return loadDocument( node, sourceName );
        if ( is( "Deduction" ) )
            return loadDeduction( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool HorrorCatalog::loadCombine( const XMLNode& node, string_view sourceName )
    {
        HorrorCombineDef combine;
        if ( GameDataXML::readFields( node, combine, sourceName ) == false )
            return false;
        RecipeDef recipe;
        recipe._id      = combine._id;
        recipe._station = getCombineStation();
        recipe._inputs.addItem( combine._firstItem, 1 );
        recipe._inputs.addItem( combine._secondItem, 1 );
        recipe._outputs.addItem( combine._outItem, combine._count );
        _recipeCatalog.add( recipe );
        return true;
    }

    bool HorrorCatalog::loadDialLock( const XMLNode& node, string_view sourceName )
    {
        HorrorDialLockDef lock;
        bool              bValid = GameDataXML::readFields( node, lock, sourceName, { "code" } );
        bValid                   = HorrorCatalogInternal::readDigits( node.getAttributeText( "code" ), lock._listDigit, sourceName, lock._id ) && bValid;
        if ( bValid )
            (void)_dialLockCatalog.add( lock ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool HorrorCatalog::loadSequence( const XMLNode& node, string_view sourceName )
    {
        HorrorSequenceDef sequence;
        if ( GameDataXML::readFields( node, sequence, sourceName, { "steps" } ) == false )
            return false;
        GameDataXML::parseNameList( node.getAttributeText( "steps" ), ",; ", sequence._listStep );
        if ( sequence._listStep.empty() )
        {
            SW_LOG_ERROR( "%#: sequence '%#' needs steps=\"a,b,...\"", sourceName, sequence._id.c_str() );
            return false;
        }
        (void)_sequenceCatalog.add( sequence ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool HorrorCatalog::loadDocument( const XMLNode& node, string_view sourceName )
    {
        HorrorDocumentDef document;
        if ( GameDataXML::readFields( node, document, sourceName, { "clues" } ) == false )
            return false;
        GameDataXML::parseNameList( node.getAttributeText( "clues" ), ",; ", document._listClue );
        (void)_documentCatalog.add( document ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool HorrorCatalog::loadDeduction( const XMLNode& node, string_view sourceName )
    {
        HorrorDeductionDef deduction;
        bool               bValid = GameDataXML::readFields( node, deduction, sourceName, { "links" } );
        bValid                    = HorrorCatalogInternal::readLinks( node.getAttributeText( "links" ), deduction._listRequiredLink, sourceName, deduction._id ) && bValid;
        if ( bValid )
            (void)_deductionCatalog.add( deduction ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    GridInventory::ShapeDelegate HorrorCatalog::makeShapeLookup() const
    {
        const HorrorCatalog* pCatalog = this;
        return GridInventory::ShapeDelegate::create(
            [pCatalog]( const hashed_string& itemID, GridItemShape& outShape )
        {
            const HorrorItemDef* pItem = pCatalog->findItem( itemID );
            if ( pItem == nullptr )
                return false;
            outShape._width    = pItem->_width;
            outShape._height   = pItem->_height;
            outShape._maxStack = pItem->_maxStack;
            return true;
        } );
    }
} // namespace sw
