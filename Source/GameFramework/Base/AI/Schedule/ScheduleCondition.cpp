#include "pch.h"

#include "GameFramework/Base/AI/Schedule/ScheduleCondition.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/Base/World/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "ScheduleCondition" );

    namespace
    {
        struct ScheduleConditionInternal
        {
            static constexpr const utf8* kNameSeparators = ",;| ";

            static constexpr const utf8* kArrConditionAttribute[] = { "days", "daysOfSeason", "seasons", "weathers", "phases", "flags", "tags", "notTags", "chance" };

            static bool contains( const vector<hashed_string>& listName, const hashed_string& name )
            {
                for ( const hashed_string& entry : listName )
                {
                    if ( entry == name )
                        return true;
                }
                return false;
            }

            /** @brief 어휘가 있으면 목록의 이름이 어휘에 있는지 봅니다. */
            static void warnUnknownNames( const vector<hashed_string>& listName, const vector<hashed_string>& listKnown, const utf8* pKind, string_view sourceName,
                                          string_view ownerName )
            {
                if ( listKnown.empty() )
                    return;
                for ( const hashed_string& name : listName )
                {
                    if ( contains( listKnown, name ) == false )
                        SW_LOG_WARNING( "%#: '%#' has an unknown %# '%#'", sourceName, ownerName, pKind, name.c_str() );
                }
            }

            static void parseTagList( string_view text, vector<TagID>& outListTag )
            {
                outListTag.clear();
                GameDataXml::forEachToken( text, kNameSeparators, [&]( string_view token )
                { outListTag.push_back( TagID::request( token ) ); } );
            }

            static bool hasTag( const ScheduleConditionContext& context, const TagID& tag )
            {
                const bool bNpc   = context._pNpcTags != nullptr && context._pNpcTags->hasTag( tag );
                const bool bWorld = context._pWorldTags != nullptr && context._pWorldTags->hasTag( tag );
                return bNpc || bWorld;
            }

            static void mark( ScheduleConditionResult& inoutResult, ScheduleConditionClause clause, bool bPassed )
            {
                const uint16 bit         = static_cast<uint16>( 1u << static_cast<uint32>( clause ) );
                inoutResult._checkedMask = static_cast<uint16>( inoutResult._checkedMask | bit );
                if ( bPassed == false )
                    inoutResult._failedMask = static_cast<uint16>( inoutResult._failedMask | bit );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( ScheduleConditionClause clause )
    {
        switch ( clause )
        {
            case ScheduleConditionClause::Weekday:
                return "days";
            case ScheduleConditionClause::DayOfSeason:
                return "daysOfSeason";
            case ScheduleConditionClause::Season:
                return "seasons";
            case ScheduleConditionClause::Weather:
                return "weathers";
            case ScheduleConditionClause::Phase:
                return "phases";
            case ScheduleConditionClause::Flags:
                return "flags";
            case ScheduleConditionClause::Tags:
                return "tags";
            case ScheduleConditionClause::Chance:
                return "chance";
            case ScheduleConditionClause::Count:
                return "count";
        }
        return "unknown";
    }

    bool ScheduleCondition::isConditionAttribute( const utf8* pName )
    {
        for ( const utf8* pKnown : ScheduleConditionInternal::kArrConditionAttribute )
        {
            if ( StringUtil::equals( pName, pKnown, true ) )
                return true;
        }
        return false;
    }

    void ScheduleCondition::parseNameList( string_view text, vector<hashed_string>& outListName )
    {
        outListName.clear();
        GameDataXml::forEachToken( text, ScheduleConditionInternal::kNameSeparators, [&]( string_view token )
        { outListName.push_back( hashed_string( token ) ); } );
    }

    uint8 ScheduleCondition::parsePhaseMask( string_view text, string_view sourceName, string_view ownerName )
    {
        uint8 mask = 0;
        GameDataXml::forEachToken( text, ScheduleConditionInternal::kNameSeparators, [&]( string_view token )
        {
            const hashed_string name( token );
            bool                bFound = false;
            for ( uint32 phaseIndex = 0; phaseIndex <= static_cast<uint32>( DayPhase::Dusk ); ++phaseIndex )
            {
                const DayPhase phase = static_cast<DayPhase>( phaseIndex );
                if ( name == hashed_string( toString( phase ) ) )
                {
                    mask   = static_cast<uint8>( mask | makeDayPhaseBit( phase ) );
                    bFound = true;
                }
            }
            if ( bFound == false )
                SW_LOG_WARNING( "%#: '%#' has an unknown phase '%#'", sourceName, ownerName, token );
        } );
        return mask;
    }

    void ScheduleCondition::readFromNode( const XmlNode& node, const ScheduleConditionVocabulary& vocabulary, string_view sourceName, string_view ownerName )
    {
        using Internal = ScheduleConditionInternal;
        parseNameList( node.getAttributeText( "days" ), _listWeekday );
        parseNameList( node.getAttributeText( "seasons" ), _listSeason );
        parseNameList( node.getAttributeText( "weathers" ), _listWeather );
        Internal::warnUnknownNames( _listWeekday, vocabulary._listWeekday, "day", sourceName, ownerName );
        Internal::warnUnknownNames( _listSeason, vocabulary._listSeason, "season", sourceName, ownerName );
        Internal::warnUnknownNames( _listWeather, vocabulary._listWeather, "weather", sourceName, ownerName );

        _listDayOfSeason.clear();
        GameDataXml::forEachToken( node.getAttributeText( "daysOfSeason" ), Internal::kNameSeparators, [&]( string_view token )
        {
            int32 dayOfSeason = 0;
            if ( StringUtil::parseInt( token, dayOfSeason ) && dayOfSeason >= 1 )
                _listDayOfSeason.push_back( dayOfSeason );
            else
                SW_LOG_WARNING( "%#: '%#' has an invalid day of season '%#'", sourceName, ownerName, token );
        } );

        _phaseMask = parsePhaseMask( node.getAttributeText( "phases" ), sourceName, ownerName );
        Internal::parseTagList( node.getAttributeText( "tags" ), _listRequiredTag );
        Internal::parseTagList( node.getAttributeText( "notTags" ), _listForbiddenTag );
        _chance = MathUtil::clamp( node.getAttributeFloat( "chance", _chance ), 0.0f, 1.0f );

        const string_view flags = node.getAttributeText( "flags" );
        _flags.assign( flags.data(), flags.size() );
        if ( _flags.empty() == false )
        {
            const GameFlags noFlags;
            bool            bIgnored = false;
            if ( GameFlags::parseCondition( _flags, noFlags, bIgnored ) == false )
                SW_LOG_WARNING( "%#: '%#' has an invalid flags expression '%#'", sourceName, ownerName, _flags );
        }
    }

    bool ScheduleCondition::isEmpty() const
    {
        const bool bNoNames = _listWeekday.empty() && _listSeason.empty() && _listWeather.empty() && _listDayOfSeason.empty();
        const bool bNoTags  = _listRequiredTag.empty() && _listForbiddenTag.empty();
        return bNoNames && bNoTags && _flags.empty() && _chance >= 1.0f && _phaseMask == 0;
    }

    ScheduleConditionResult ScheduleCondition::evaluate( const ScheduleConditionContext& context ) const
    {
        using Internal = ScheduleConditionInternal;
        ScheduleConditionResult result;
        if ( _listWeekday.empty() == false )
            Internal::mark( result, ScheduleConditionClause::Weekday, Internal::contains( _listWeekday, context._weekday ) );
        if ( _listDayOfSeason.empty() == false )
        {
            bool bFound = false;
            for ( const int32 dayOfSeason : _listDayOfSeason )
                bFound = bFound || dayOfSeason == context._dayOfSeason;
            Internal::mark( result, ScheduleConditionClause::DayOfSeason, bFound );
        }
        if ( _listSeason.empty() == false )
            Internal::mark( result, ScheduleConditionClause::Season, Internal::contains( _listSeason, context._season ) );
        if ( _listWeather.empty() == false )
            Internal::mark( result, ScheduleConditionClause::Weather, Internal::contains( _listWeather, context._weather ) );
        if ( _phaseMask != 0 )
            Internal::mark( result, ScheduleConditionClause::Phase, ( _phaseMask & makeDayPhaseBit( context._phase ) ) != 0 );
        if ( _flags.empty() == false )
            Internal::mark( result, ScheduleConditionClause::Flags, context._pFlags != nullptr && context._pFlags->evaluate( _flags ) );
        if ( _listRequiredTag.empty() == false || _listForbiddenTag.empty() == false )
        {
            bool bPassed = true;
            for ( const TagID& tag : _listRequiredTag )
                bPassed = bPassed && Internal::hasTag( context, tag );
            for ( const TagID& tag : _listForbiddenTag )
                bPassed = bPassed && Internal::hasTag( context, tag ) == false;
            Internal::mark( result, ScheduleConditionClause::Tags, bPassed );
        }
        if ( _chance < 1.0f )
        {
            const float32 roll = GameHash::toUnitFloat( GameHash::hashCoord( context._day, static_cast<int32>( context._chanceKey ), 0x5c4ed01eu ) );
            Internal::mark( result, ScheduleConditionClause::Chance, roll < _chance );
        }
        return result;
    }
} // namespace sw
