#include "pch.h"

#include "GameFramework/Kits/Fighting/FighterCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "FighterCatalog" );

    namespace
    {
        struct FighterCatalogInternal
        {
            /** @brief "Standing,Crouching,Airborne,Stance" 를 비트로 읽습니다. 모르는 낱말은 경고합니다. 비면 @p fallback 입니다. */
            static uint8 parsePostureMask( string_view text, uint8 fallback, string_view sourceName, const utf8* pMoveId )
            {
                uint8 mask = 0;
                GameDataXml::forEachToken( text, ",; \t", [&]( string_view token )
                {
                    if ( StringUtil::equals( token, "Standing", true ) )
                        mask = static_cast<uint8>( mask | ( 1u << static_cast<uint32>( FighterPosture::Standing ) ) );
                    else if ( StringUtil::equals( token, "Crouching", true ) )
                        mask = static_cast<uint8>( mask | ( 1u << static_cast<uint32>( FighterPosture::Crouching ) ) );
                    else if ( StringUtil::equals( token, "Airborne", true ) )
                        mask = static_cast<uint8>( mask | ( 1u << static_cast<uint32>( FighterPosture::Airborne ) ) );
                    else if ( StringUtil::equals( token, "Stance", true ) )
                        mask = static_cast<uint8>( mask | ( 1u << static_cast<uint32>( FighterPosture::Stance ) ) );
                    else
                        SW_LOG_WARNING( "%#: move '%#' has an unknown posture '%#' - ignored", sourceName, pMoveId, string( token ) );
                } );
                return mask != 0 ? mask : fallback;
            }

            /** @brief 커맨드 표기를 읽습니다. 비면 @p pDefault 를 씁니다. */
            [[nodiscard]] static bool parseCommandOr( const InputCommandParser& parser, string_view text, const utf8* pDefault, InputCommand& outCommand )
            {
                const string_view notation = text.empty() ? string_view( pDefault ) : text;
                return parser.parse( notation, outCommand );
            }

            static void addStance( FighterDef& fighter, const hashed_string& stance )
            {
                if ( stance.empty() || fighter.findStanceIndex( stance ) >= 0 )
                    return;
                fighter._listStance.push_back( stance );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // FighterDef
    // ------------------------------------------------------------------------------
    int32 FighterDef::findMoveIndex( const hashed_string& moveId ) const
    {
        for ( size_t index = 0; index < _listMove.size(); ++index )
        {
            if ( _listMove[index]._frame._id == moveId )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 FighterDef::findStanceIndex( const hashed_string& stance ) const
    {
        for ( size_t index = 0; index < _listStance.size(); ++index )
        {
            if ( _listStance[index] == stance )
                return static_cast<int32>( index );
        }
        return -1;
    }

    // ------------------------------------------------------------------------------
    // FighterCatalog
    // ------------------------------------------------------------------------------
    FighterCatalog::FighterCatalog()
        : _catalog{}
    {
    }

    bool FighterCatalog::loadFromResource( string_view path, const MoveCatalog& moveCatalog )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "FighterCatalog", root, sourceName ) && loadRoot( root, moveCatalog, sourceName ) > 0;
    }

    bool FighterCatalog::loadFromXmlText( string_view xmlText, const MoveCatalog& moveCatalog, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "FighterCatalog", root ) && loadRoot( root, moveCatalog, sourceName ) > 0;
    }

    void FighterCatalog::addFighter( const FighterDef& fighter )
    {
        FighterDef copy = fighter;
        for ( const FighterMove& move : fighter._listMove )
            FighterCatalogInternal::addStance( copy, move._stance );
        for ( const FighterMove& move : fighter._listMove )
            FighterCatalogInternal::addStance( copy, move._enterStance );
        (void)_catalog.add( copy ); // 빈 id 는 카탈로그가 거른다
    }

    uint32 FighterCatalog::loadRoot( const XmlNode& root, const MoveCatalog& moveCatalog, string_view sourceName )
    {
        InputCommandParser parser;
        const string_view  buttonNames = root.getAttributeText( "buttons" );
        if ( buttonNames.empty() == false )
            parser.setButtonNames( buttonNames );

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Fighter" ); node; node = node.findNextSibling( "Fighter" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            FighterDef fighter;
            fighter._id             = hashed_string( pId );
            const utf8* pName       = node.findAttribute( "name" );
            fighter._name           = pName != nullptr ? pName : pId;
            fighter._health         = MathUtil::max( 1, node.getAttributeInt( "health", fighter._health ) );
            fighter._sidestepFrames = MathUtil::max( 1, node.getAttributeInt( "sidestepFrames", fighter._sidestepFrames ) );
            fighter._jumpFrames     = MathUtil::max( 2, node.getAttributeInt( "jumpFrames", fighter._jumpFrames ) );
            fighter._walkSpeed      = MathUtil::max( 0.0f, node.getAttributeFloat( "walkSpeed", fighter._walkSpeed ) );
            fighter._hurtRadius     = MathUtil::max( 0.01f, node.getAttributeFloat( "hurtRadius", fighter._hurtRadius ) );
            fighter._pushRadius     = MathUtil::max( 0.0f, node.getAttributeFloat( "pushRadius", fighter._pushRadius ) );
            fighter._standHeight    = MathUtil::max( 0.1f, node.getAttributeFloat( "standHeight", fighter._standHeight ) );
            fighter._crouchHeight   = MathUtil::clamp( node.getAttributeFloat( "crouchHeight", fighter._crouchHeight ), 0.1f, fighter._standHeight );
            fighter._sidestepAngle  = node.getAttributeFloat( "sidestepAngle", fighter._sidestepAngle );
            fighter._jumpHeight     = MathUtil::max( 0.0f, node.getAttributeFloat( "jumpHeight", fighter._jumpHeight ) );
            fighter._jumpDistance   = node.getAttributeFloat( "jumpDistance", fighter._jumpDistance );
            const bool bSidestepOk  = FighterCatalogInternal::parseCommandOr( parser, node.getAttributeText( "sidestep" ), "u", fighter._sidestepUp );
            const bool bSidestepDownOk =
                FighterCatalogInternal::parseCommandOr( parser, node.getAttributeText( "sidestepDown" ), "d,n", fighter._sidestepDown );
            const bool bJumpOk = FighterCatalogInternal::parseCommandOr( parser, node.getAttributeText( "jump" ), "u/f", fighter._jump );
            if ( bSidestepOk == false || bSidestepDownOk == false || bJumpOk == false )
                SW_LOG_WARNING( "%#: fighter '%#' has an unreadable sidestep/jump command - movement disabled", sourceName, pId );

            for ( XmlNode child = node.findChild( "Move" ); child; child = child.findNextSibling( "Move" ) )
            {
                const utf8* pMoveId = GameDataXml::findRequiredId( child, sourceName );
                if ( pMoveId == nullptr )
                    continue;
                const string_view    framesId = child.getAttributeText( "frames" );
                const MoveFrameData* pFrame   = moveCatalog.findMove( hashed_string( framesId.empty() ? string_view( pMoveId ) : framesId ) );
                if ( pFrame == nullptr )
                {
                    SW_LOG_WARNING( "%#: fighter '%#' move '%#' has no frame data in the MoveCatalog - skipped", sourceName, pId, pMoveId );
                    continue;
                }
                FighterMove move;
                move._frame     = *pFrame;
                move._frame._id = hashed_string( pMoveId );
                if ( parser.parse( child.getAttributeText( "command" ), move._command ) == false )
                {
                    SW_LOG_WARNING( "%#: fighter '%#' move '%#' has an unreadable command - skipped", sourceName, pId, pMoveId );
                    continue;
                }
                move._command._id        = move._frame._id;
                move._command._priority  = child.getAttributeInt( "priority", 0 );
                move._command._maxGap    = MathUtil::max( 1, child.getAttributeInt( "maxGap", move._command._maxGap ) );
                const string_view stance = child.getAttributeText( "stance" );
                if ( stance.empty() == false )
                {
                    move._stance      = hashed_string( stance );
                    move._postureMask = static_cast<uint8>( 1u << static_cast<uint32>( FighterPosture::Stance ) );
                }
                else
                {
                    move._postureMask = FighterCatalogInternal::parsePostureMask( child.getAttributeText( "from" ), move._postureMask, sourceName, pMoveId );
                }
                const string_view enterStance = child.getAttributeText( "enterStance" );
                if ( enterStance.empty() == false )
                    move._enterStance = hashed_string( enterStance );
                const string_view breakButtons = child.getAttributeText( "breakButtons" );
                if ( breakButtons.empty() == false )
                {
                    InputCommand breakCommand;
                    if ( parser.parse( breakButtons, breakCommand ) && breakCommand._listStep.size() == 1 )
                        move._breakButtons = breakCommand._listStep[0]._buttons;
                    else
                        SW_LOG_WARNING( "%#: fighter '%#' move '%#' has unreadable breakButtons - throw cannot be broken", sourceName, pId, pMoveId );
                }
                move._pushback    = MathUtil::max( 0.0f, child.getAttributeFloat( "pushback", move._pushback ) );
                move._bStringOnly = child.getAttributeBool( "stringOnly", false ) ? SW_TRUE : SW_FALSE;
                move._bTracking   = child.getAttributeBool( "tracking", false ) ? SW_TRUE : SW_FALSE;
                move._bScrew      = child.getAttributeBool( "screw", false ) ? SW_TRUE : SW_FALSE;
                move._bBound      = child.getAttributeBool( "bound", false ) ? SW_TRUE : SW_FALSE;
                move._bGroundHit  = child.getAttributeBool( "groundHit", false ) ? SW_TRUE : SW_FALSE;
                move._bNearWall   = child.getAttributeBool( "nearWall", false ) ? SW_TRUE : SW_FALSE;
                move._bRageArt    = child.getAttributeBool( "rageArt", false ) ? SW_TRUE : SW_FALSE;
                move._bHeatBurst  = child.getAttributeBool( "heatBurst", false ) ? SW_TRUE : SW_FALSE;
                fighter._listMove.push_back( move );
            }
            if ( fighter._listMove.empty() )
                SW_LOG_WARNING( "%#: fighter '%#' has no usable <Move>", sourceName, pId );
            addFighter( fighter );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Fighter> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
