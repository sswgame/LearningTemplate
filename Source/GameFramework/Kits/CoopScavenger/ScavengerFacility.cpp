#include "pch.h"

#include "GameFramework/Kits/CoopScavenger/ScavengerFacility.h"

#include "Core/Common/Defines.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/GameRandom.h"
#include "GameFramework/Kits/CoopScavenger/ScavengerCatalog.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ScavengerFacilityInternal
        {
            static void appendNumber( string& inoutText, int32 value )
            {
                utf8         arrBuffer[constant::kMaxBuffer32];
                const uint32 length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, value );
                inoutText.append( arrBuffer, length );
            }

            static void appendRoomId( string& inoutText, int32 roomIndex )
            {
                if ( roomIndex == 0 )
                {
                    inoutText += "entrance";
                    return;
                }
                inoutText += "room";
                appendNumber( inoutText, roomIndex );
            }

            static hashed_string makeRoomId( int32 roomIndex )
            {
                string text;
                appendRoomId( text, roomIndex );
                return hashed_string( string_view( text.data(), text.size() ) );
            }

            static void appendArea( string& inoutText, const utf8* pId, int32 roomIndex, const utf8* pRegion, int32 x, int32 y )
            {
                inoutText += "<Area id=\"";
                if ( pId != nullptr )
                    inoutText += pId;
                else
                    appendRoomId( inoutText, roomIndex );
                inoutText += "\" region=\"";
                inoutText += pRegion;
                inoutText += "\" x=\"";
                appendNumber( inoutText, x );
                inoutText += "\" y=\"";
                appendNumber( inoutText, y );
                inoutText += "\"/>";
            }

            /** @brief 방 번호가 음수면 바깥(`outside`)입니다. */
            static void appendLink( string& inoutText, int32 fromRoom, int32 toRoom, const utf8* pKind, bool bLocked )
            {
                inoutText += "<Link from=\"";
                if ( fromRoom < 0 )
                    inoutText += "outside";
                else
                    appendRoomId( inoutText, fromRoom );
                inoutText += "\" to=\"";
                appendRoomId( inoutText, toRoom );
                inoutText += "\" kind=\"";
                inoutText += pKind;
                if ( bLocked )
                {
                    inoutText += "\" requires=\"unlocked_";
                    appendRoomId( inoutText, toRoom );
                }
                inoutText += "\"/>";
            }

            static const ScavengerScrapDef* pickScrap( const ScavengerCatalog& catalog, const ScavengerMoonDef& moon, GameRandom& random )
            {
                float32 total = 0.0f;
                if ( moon._listScrap.empty() )
                {
                    for ( const ScavengerScrapDef& def : catalog.getScraps() )
                        total += def._spawnWeight;
                }
                else
                {
                    for ( const hashed_string& scrapId : moon._listScrap )
                        total += catalog.findScrap( scrapId )->_spawnWeight;
                }
                if ( total <= 0.0f )
                    return nullptr;
                float32 pick = random.nextFloat() * total;
                if ( moon._listScrap.empty() )
                {
                    for ( const ScavengerScrapDef& def : catalog.getScraps() )
                    {
                        pick -= def._spawnWeight;
                        if ( pick < 0.0f && def._spawnWeight > 0.0f )
                            return &def;
                    }
                    return &catalog.getScraps().back();
                }
                for ( const hashed_string& scrapId : moon._listScrap )
                {
                    const ScavengerScrapDef* pDef = catalog.findScrap( scrapId );
                    pick -= pDef->_spawnWeight;
                    if ( pick < 0.0f && pDef->_spawnWeight > 0.0f )
                        return pDef;
                }
                return catalog.findScrap( moon._listScrap.back() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ScavengerFacility::ScavengerFacility()
        : _graph{}
        , _flags{}
        , _listGroundScrap{}
        , _nextUid{ 1 }
        , _roomCount{ 0 }
        , _lockedDoorCount{ 0 }
    {
    }

    bool ScavengerFacility::createLayout( const ScavengerCatalog& catalog, const ScavengerMoonDef& moon, uint32 seed, float32 valueScale )
    {
        clear();
        GameRandom                       random{ seed };
        const ScavengerFacilitySettings& settings = catalog.getFacilitySettings();
        string                           xml      = "<AreaGraph>";
        ScavengerFacilityInternal::appendArea( xml, "outside", 0, "Outside", 0, 0 );
        ScavengerFacilityInternal::appendArea( xml, "ship", 0, "Ship", -1, 0 );
        if ( moon._bCompany == SW_FALSE )
        {
            _roomCount = random.nextInt( settings._minRooms, settings._maxRooms );
            vector<int32> listDepth( static_cast<size_t>( _roomCount ), 0 );
            vector<int32> listParent( static_cast<size_t>( _roomCount ), -1 );
            for ( int32 room = 1; room < _roomCount; ++room )
            {
                listParent[static_cast<size_t>( room )] = random.nextInt( 0, room - 1 );
                listDepth[static_cast<size_t>( room )]  = listDepth[static_cast<size_t>( listParent[static_cast<size_t>( room )] )] + 1;
            }
            for ( int32 room = 0; room < _roomCount; ++room )
                ScavengerFacilityInternal::appendArea( xml, nullptr, room, "Facility", listDepth[static_cast<size_t>( room )] + 1, room );

            xml += "<Link from=\"outside\" to=\"ship\" kind=\"Ship\"/>";
            ScavengerFacilityInternal::appendLink( xml, -1, 0, "MainEntrance", false );
            for ( int32 room = 1; room < _roomCount; ++room )
            {
                const bool bLocked = random.nextChance( settings._lockedChance );
                _lockedDoorCount += bLocked ? 1 : 0;
                ScavengerFacilityInternal::appendLink( xml, listParent[static_cast<size_t>( room )], room, "Door", bLocked );
            }
            for ( int32 room = 2; room < _roomCount; ++room )
            {
                if ( random.nextChance( settings._loopChance ) == false )
                    continue;
                const int32 other = random.nextInt( 0, room - 1 );
                if ( other != listParent[static_cast<size_t>( room )] )
                    ScavengerFacilityInternal::appendLink( xml, other, room, "Door", false );
            }
            // 화재 출구 — 정문에서 가장 깊은 방부터(같으면 번호 순).
            vector<int32> listExitCandidate;
            for ( int32 room = 1; room < _roomCount; ++room )
                listExitCandidate.push_back( room );
            std::stable_sort( listExitCandidate.begin(), listExitCandidate.end(),
                              [&]( int32 lhs, int32 rhs )
            { return listDepth[static_cast<size_t>( lhs )] > listDepth[static_cast<size_t>( rhs )]; } );
            const int32 exitCount = MathUtil::min( settings._fireExits, static_cast<int32>( listExitCandidate.size() ) );
            for ( int32 exitIndex = 0; exitIndex < exitCount; ++exitIndex )
                ScavengerFacilityInternal::appendLink( xml, -1, listExitCandidate[static_cast<size_t>( exitIndex )], "FireExit", false );
        }
        else
        {
            xml += "<Link from=\"outside\" to=\"ship\" kind=\"Ship\"/>";
        }
        xml += "</AreaGraph>";
        if ( _graph.loadFromXmlText( string_view( xml.data(), xml.size() ), "ScavengerFacility" ) == false )
            return false;

        if ( _roomCount <= 0 || catalog.getScraps().empty() )
            return true;
        const int32 scrapCount = random.nextInt( moon._minScrap, moon._maxScrap );
        for ( int32 scrapIndex = 0; scrapIndex < scrapCount; ++scrapIndex )
        {
            const int32              room = random.nextInt( 0, _roomCount - 1 );
            const ScavengerScrapDef* pDef = ScavengerFacilityInternal::pickScrap( catalog, moon, random );
            if ( pDef == nullptr )
                break;
            const float32  baseValue = static_cast<float32>( random.nextInt( pDef->_minValue, pDef->_maxValue ) );
            const float32  moonScale = moon._maxValueScale > moon._minValueScale ? random.nextRange( moon._minValueScale, moon._maxValueScale ) : moon._minValueScale;
            ScavengerScrap scrap;
            scrap._scrapId    = pDef->_id;
            scrap._weight     = pDef->_weight;
            scrap._value      = static_cast<int32>( baseValue * moonScale * MathUtil::max( 0.0f, valueScale ) + 0.5f );
            scrap._bTwoHanded = pDef->_bTwoHanded;
            (void)placeScrap( scrap, ScavengerFacilityInternal::makeRoomId( room ) );
        }
        return true;
    }

    void ScavengerFacility::clear()
    {
        _flags.clear();
        _listGroundScrap.clear();
        _roomCount       = 0;
        _lockedDoorCount = 0;
    }

    bool ScavengerFacility::unlockDoor( const hashed_string& roomId )
    {
        string flagText = "unlocked_";
        flagText += roomId.c_str();
        const hashed_string flag( string_view( flagText.data(), flagText.size() ) );
        if ( _flags.hasFlag( flag ) )
            return false;
        for ( const AreaLink& link : _graph.getLinks() )
        {
            if ( link._to == roomId && link._requires.empty() == false )
            {
                _flags.setFlag( flag );
                return true;
            }
        }
        return false;
    }

    bool ScavengerFacility::canTraverse( const hashed_string& fromId, const hashed_string& toId ) const { return _graph.canTraverse( fromId, toId, _flags ); }

    int32 ScavengerFacility::placeScrap( const ScavengerScrap& scrap, const hashed_string& areaId )
    {
        ScavengerScrap placed = scrap;
        placed._areaId        = areaId;
        if ( placed._uid <= 0 )
            placed._uid = _nextUid++;
        _listGroundScrap.push_back( placed );
        return placed._uid;
    }

    bool ScavengerFacility::tryTakeScrap( int32 uid, const hashed_string& areaId, ScavengerScrap& outScrap )
    {
        for ( size_t index = 0; index < _listGroundScrap.size(); ++index )
        {
            if ( _listGroundScrap[index]._uid != uid || _listGroundScrap[index]._areaId != areaId )
                continue;
            outScrap         = _listGroundScrap[index];
            outScrap._areaId = hashed_string{};
            _listGroundScrap.erase( _listGroundScrap.begin() + static_cast<ptrdiff_t>( index ) );
            return true;
        }
        return false;
    }

    const ScavengerScrap* ScavengerFacility::findGroundScrap( int32 uid ) const
    {
        for ( const ScavengerScrap& scrap : _listGroundScrap )
        {
            if ( scrap._uid == uid )
                return &scrap;
        }
        return nullptr;
    }

    int32 ScavengerFacility::computeGroundValue() const
    {
        int32 total = 0;
        for ( const ScavengerScrap& scrap : _listGroundScrap )
            total += scrap.isBody() ? 0 : scrap._value;
        return total;
    }
} // namespace sw
