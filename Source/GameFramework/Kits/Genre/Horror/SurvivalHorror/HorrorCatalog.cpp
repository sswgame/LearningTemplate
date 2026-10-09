#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/HorrorCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "HorrorCatalog" );

    namespace
    {
        struct HorrorCatalogInternal
        {
            static constexpr const utf8* kArrItemKindName[] = { "Misc", "Weapon", "Ammo", "Heal", "Key", "SaveItem", "Battery", "Document" };

            static void parseIdList( string_view text, vector<hashed_string>& outListId )
            {
                outListId.clear();
                GameDataXml::forEachToken( text, ",; ", [&]( string_view token )
                { outListId.push_back( hashed_string( token ) ); } );
            }

            static void parseDigits( string_view text, vector<int32>& outListDigit, string_view sourceName, const utf8* pId )
            {
                outListDigit.clear();
                GameDataXml::forEachToken( text, ",; ", [&]( string_view token )
                {
                    int32 digit = 0;
                    if ( StringUtil::parseInt( token, digit ) )
                        outListDigit.push_back( digit );
                    else
                        SW_LOG_WARNING( "%#: dial lock '%#' has a bad digit '%#'", sourceName, pId, token );
                } );
            }

            /** @brief "a-b,c-d" 를 연결 목록으로 읽습니다. */
            static void parseLinks( string_view text, vector<HorrorClueLink>& outListLink, string_view sourceName, const utf8* pId )
            {
                outListLink.clear();
                GameDataXml::forEachToken( text, ",; ", [&]( string_view token )
                {
                    const size_t dash = token.find( '-' );
                    if ( dash == string_view::npos || dash == 0 || dash + 1 >= token.size() )
                    {
                        SW_LOG_WARNING( "%#: deduction '%#' has a bad link '%#' (expected clueA-clueB)", sourceName, pId, token );
                        return;
                    }
                    HorrorClueLink link;
                    link._first  = hashed_string( token.substr( 0, dash ) );
                    link._second = hashed_string( token.substr( dash + 1 ) );
                    outListLink.push_back( link );
                } );
            }

            static void loadRules( const XmlNode& node, SurvivalHorrorRules& outRules, string_view sourceName )
            {
                outRules._maxHealth              = MathUtil::max( 1.0f, node.getAttributeFloat( "maxHealth", outRules._maxHealth ) );
                outRules._maxSanity              = MathUtil::max( 1.0f, node.getAttributeFloat( "maxSanity", outRules._maxSanity ) );
                outRules._darknessDrain          = MathUtil::max( 0.0f, node.getAttributeFloat( "darknessDrain", outRules._darknessDrain ) );
                outRules._sanityRegen            = MathUtil::max( 0.0f, node.getAttributeFloat( "sanityRegen", outRules._sanityRegen ) );
                outRules._sanityRegenDelay       = MathUtil::max( 0.0f, node.getAttributeFloat( "sanityRegenDelay", outRules._sanityRegenDelay ) );
                outRules._repeatSightingScale    = MathUtil::saturate( node.getAttributeFloat( "repeatSightingScale", outRules._repeatSightingScale ) );
                outRules._hallucinationThreshold = MathUtil::saturate( node.getAttributeFloat( "hallucinationThreshold", outRules._hallucinationThreshold ) );
                outRules._maxAimSway             = MathUtil::max( 0.0f, node.getAttributeFloat( "maxAimSway", outRules._maxAimSway ) );
                outRules._maxBattery             = MathUtil::max( 1.0f, node.getAttributeFloat( "maxBattery", outRules._maxBattery ) );
                outRules._batteryDrain           = MathUtil::max( 0.0f, node.getAttributeFloat( "batteryDrain", outRules._batteryDrain ) );
                outRules._gridWidth              = MathUtil::clamp( node.getAttributeInt( "gridWidth", outRules._gridWidth ), 1, 32 );
                outRules._gridHeight             = MathUtil::clamp( node.getAttributeInt( "gridHeight", outRules._gridHeight ), 1, 32 );
                outRules._maxSaves               = MathUtil::max( 0, node.getAttributeInt( "maxSaves", outRules._maxSaves ) );
                outRules._bClearLinksOnWrong     = node.getAttributeBool( "clearLinksOnWrong", outRules._bClearLinksOnWrong == SW_TRUE ) ? SW_TRUE : SW_FALSE;
                const string_view modeText       = node.getAttributeText( "saveMode" );
                if ( modeText.empty() )
                    return;
                if ( StringUtil::equals( modeText, string_view( "Unlimited" ), true ) )
                    outRules._saveMode = HorrorSaveMode::Unlimited;
                else if ( StringUtil::equals( modeText, string_view( "Limited" ), true ) )
                    outRules._saveMode = HorrorSaveMode::Limited;
                else if ( StringUtil::equals( modeText, string_view( "InkRibbon" ), true ) )
                    outRules._saveMode = HorrorSaveMode::InkRibbon;
                else
                    SW_LOG_WARNING( "%#: unknown saveMode '%#'", sourceName, modeText );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool parseHorrorItemKind( string_view text, HorrorItemKind& outKind )
    {
        for ( uint32 kindIndex = 0; kindIndex < 8; ++kindIndex )
        {
            if ( StringUtil::equals( text, string_view( HorrorCatalogInternal::kArrItemKindName[kindIndex] ), true ) )
            {
                outKind = static_cast<HorrorItemKind>( kindIndex );
                return true;
            }
        }
        return false;
    }

    const utf8* toString( HorrorItemKind kind )
    {
        const uint32 kindIndex = static_cast<uint32>( kind );
        return kindIndex < 8 ? HorrorCatalogInternal::kArrItemKindName[kindIndex] : "?";
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
        for ( const RecipeDef& recipe : _recipeCatalog.getRecipes() )
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

    uint32 HorrorCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        if ( const XmlNode rulesNode = root.findChild( "Rules" ) )
            HorrorCatalogInternal::loadRules( rulesNode, _rules, sourceName );

        for ( XmlNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HorrorItemDef item;
            item._id                   = hashed_string( pId );
            const utf8* pName          = node.findAttribute( "name" );
            item._name                 = pName != nullptr ? pName : pId;
            const string_view kindText = node.getAttributeText( "kind" );
            if ( kindText.empty() == false && parseHorrorItemKind( kindText, item._kind ) == false )
                SW_LOG_WARNING( "%#: item '%#' has an unknown kind '%#'", sourceName, pId, kindText );
            item._width         = MathUtil::clamp( node.getAttributeInt( "w", item._width ), 1, 8 );
            item._height        = MathUtil::clamp( node.getAttributeInt( "h", item._height ), 1, 8 );
            item._maxStack      = MathUtil::max( 1, node.getAttributeInt( "maxStack", item._maxStack ) );
            item._healAmount    = MathUtil::max( 0.0f, node.getAttributeFloat( "heal", item._healAmount ) );
            item._sanityAmount  = MathUtil::max( 0.0f, node.getAttributeFloat( "sanity", item._sanityAmount ) );
            item._batteryAmount = MathUtil::max( 0.0f, node.getAttributeFloat( "battery", item._batteryAmount ) );
            (void)_itemCatalog.add( item );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Combine" ); node; node = node.findNextSibling( "Combine" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            const hashed_string firstItem( node.getAttributeText( "a" ) );
            const hashed_string secondItem( node.getAttributeText( "b" ) );
            const hashed_string outItem( node.getAttributeText( "out" ) );
            if ( firstItem.empty() || secondItem.empty() || outItem.empty() )
            {
                SW_LOG_WARNING( "%#: combine '%#' needs a, b and out - skipped", sourceName, pId );
                continue;
            }
            RecipeDef recipe;
            recipe._id      = hashed_string( pId );
            recipe._station = getCombineStation();
            recipe._inputs.addItem( firstItem, 1 );
            recipe._inputs.addItem( secondItem, 1 );
            recipe._outputs.addItem( outItem, MathUtil::max( 1, node.getAttributeInt( "count", 1 ) ) );
            _recipeCatalog.addRecipe( recipe );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Monster" ); node; node = node.findNextSibling( "Monster" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HorrorMonsterDef monster;
            monster._id         = hashed_string( pId );
            monster._sanityLoss = MathUtil::max( 0.0f, node.getAttributeFloat( "sanityLoss", monster._sanityLoss ) );
            monster._speed      = MathUtil::max( 0.01f, node.getAttributeFloat( "speed", monster._speed ) );
            monster._toughness  = MathUtil::max( 1, node.getAttributeInt( "toughness", monster._toughness ) );
            monster._damage     = MathUtil::max( 0, node.getAttributeInt( "damage", monster._damage ) );
            monster._horror     = MathUtil::max( 0, node.getAttributeInt( "horror", monster._horror ) );
            (void)_monsterCatalog.add( monster );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "KeyLock" ); node; node = node.findNextSibling( "KeyLock" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HorrorKeyLockDef lock;
            lock._id          = hashed_string( pId );
            lock._keyItem     = hashed_string( node.getAttributeText( "key" ) );
            lock._flag        = hashed_string( node.getAttributeText( "flag" ) );
            lock._bConsumeKey = node.getAttributeBool( "consumeKey", true ) ? SW_TRUE : SW_FALSE;
            if ( lock._keyItem.empty() || lock._flag.empty() )
                SW_LOG_WARNING( "%#: key lock '%#' needs key and flag", sourceName, pId );
            else if ( _itemCatalog.find( lock._keyItem ) == nullptr )
                SW_LOG_WARNING( "%#: key lock '%#' names an unknown key item '%#'", sourceName, pId, lock._keyItem.c_str() );
            (void)_keyLockCatalog.add( lock );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "DialLock" ); node; node = node.findNextSibling( "DialLock" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HorrorDialLockDef lock;
            lock._id          = hashed_string( pId );
            lock._flag        = hashed_string( node.getAttributeText( "flag" ) );
            lock._maxAttempts = MathUtil::max( 0, node.getAttributeInt( "attempts", 0 ) );
            HorrorCatalogInternal::parseDigits( node.getAttributeText( "code" ), lock._listDigit, sourceName, pId );
            if ( lock._listDigit.empty() )
                SW_LOG_WARNING( "%#: dial lock '%#' has no code", sourceName, pId );
            (void)_dialLockCatalog.add( lock );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Sequence" ); node; node = node.findNextSibling( "Sequence" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HorrorSequenceDef sequence;
            sequence._id            = hashed_string( pId );
            sequence._flag          = hashed_string( node.getAttributeText( "flag" ) );
            sequence._mistakeSanity = MathUtil::max( 0.0f, node.getAttributeFloat( "mistakeSanity", 0.0f ) );
            HorrorCatalogInternal::parseIdList( node.getAttributeText( "steps" ), sequence._listStep );
            if ( sequence._listStep.empty() )
                SW_LOG_WARNING( "%#: sequence '%#' has no steps", sourceName, pId );
            (void)_sequenceCatalog.add( sequence );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Document" ); node; node = node.findNextSibling( "Document" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HorrorDocumentDef document;
            document._id       = hashed_string( pId );
            const utf8* pTitle = node.findAttribute( "title" );
            document._title    = pTitle != nullptr ? pTitle : pId;
            HorrorCatalogInternal::parseIdList( node.getAttributeText( "clues" ), document._listClue );
            (void)_documentCatalog.add( document );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Deduction" ); node; node = node.findNextSibling( "Deduction" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HorrorDeductionDef deduction;
            deduction._id          = hashed_string( pId );
            deduction._answer      = hashed_string( node.getAttributeText( "answer" ) );
            deduction._flag        = hashed_string( node.getAttributeText( "flag" ) );
            deduction._wrongSanity = MathUtil::max( 0.0f, node.getAttributeFloat( "wrongSanity", deduction._wrongSanity ) );
            HorrorCatalogInternal::parseLinks( node.getAttributeText( "links" ), deduction._listRequiredLink, sourceName, pId );
            if ( deduction._answer.empty() )
                SW_LOG_WARNING( "%#: deduction '%#' has no answer", sourceName, pId );
            (void)_deductionCatalog.add( deduction );
            ++loadedCount;
        }
        return loadedCount;
    }
    GridInventory::ShapeDelegate HorrorCatalog::makeShapeLookup() const
    {
        const HorrorCatalog* pCatalog = this;
        return GridInventory::ShapeDelegate::create(
            [pCatalog]( const hashed_string& itemId, GridItemShape& outShape )
        {
            const HorrorItemDef* pItem = pCatalog->findItem( itemId );
            if ( pItem == nullptr )
                return false;
            outShape._width    = pItem->_width;
            outShape._height   = pItem->_height;
            outShape._maxStack = pItem->_maxStack;
            return true;
        } );
    }
} // namespace sw
