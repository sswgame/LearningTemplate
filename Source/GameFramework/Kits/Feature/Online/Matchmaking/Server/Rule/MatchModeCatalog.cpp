#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "MatchModeCatalog" );

    namespace
    {
        /** @brief 빈 글이면 그대로 두고 true, 정수가 아니면 false 입니다. */
        [[nodiscard]] bool readInt64( const XMLNode& node, const utf8* pName, int64& inoutValue )
        {
            const string_view text = node.getAttributeText( pName );
            return text.empty() || StringUtil::parseInt64( text, inoutValue );
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool MatchModeCatalog::parseCompactMode( string_view text, MatchModeDefinition& outMode )
    {
        const size_t colon = text.find( ':' );
        if ( colon == string_view::npos || colon == 0 )
            return false;
        const string_view shape = text.substr( colon + 1 );
        const size_t      cross = shape.find( 'x' );
        if ( cross == string_view::npos )
            return false;
        int32      teamCount     = 0;
        int32      teamSize      = 0;
        const bool bNumbers      = StringUtil::parseInt( shape.substr( 0, cross ), teamCount ) && StringUtil::parseInt( shape.substr( cross + 1 ), teamSize );
        const bool bInvalidShape = bNumbers == false || teamCount <= 0 || teamSize <= 0;
        if ( bInvalidShape )
            return false;
        outMode._modeID    = string( text.substr( 0, colon ) );
        outMode._teamCount = teamCount;
        outMode._teamSize  = teamSize;
        return true;
    }

    uint32 MatchModeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Mode" ); node; node = node.findNextSibling( "Mode" ) )
        {
            const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            MatchModeDefinition mode;
            mode._modeID                 = pID;
            mode._teamCount              = node.getAttributeInt( "teamCount", mode._teamCount );
            mode._teamSize               = node.getAttributeInt( "teamSize", mode._teamSize );
            mode._baseRatingWindow       = node.getAttributeInt( "baseRatingWindow", mode._baseRatingWindow );
            mode._ratingWindowPerSecond  = node.getAttributeInt( "ratingWindowPerSecond", mode._ratingWindowPerSecond );
            mode._maxRatingWindow        = node.getAttributeInt( "maxRatingWindow", mode._maxRatingWindow );
            mode._buildVersion           = static_cast<uint32>( node.getAttributeInt( "buildVersion", 0 ) );
            const string_view serverKind = node.getAttributeText( "serverKind" );
            if ( serverKind.empty() == false )
                mode._serverKind = string( serverKind );
            const bool bTimesOk = readInt64( node, "regionRelaxMs", mode._regionRelaxMs ) &&
                                  readInt64( node, "maxWaitMs", mode._maxWaitMs );
            const bool bInvalidModeValues = bTimesOk == false || mode._teamCount <= 0 || mode._teamSize <= 0;
            if ( bInvalidModeValues )
            {
                SW_LOG_WARNING( "%#: mode '%#' needs positive team counts and integer times - skipped", sourceName, pID );
                continue;
            }
            _listMode.push_back( std::move( mode ) );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Mode> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
