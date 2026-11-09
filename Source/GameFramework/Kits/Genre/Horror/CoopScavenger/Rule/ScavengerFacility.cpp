#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Rule/ScavengerFacility.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLWriter.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ScavengerFacilityInternal
        {
            static void appendNumber( string& inoutText, int32 value )
            {
                utf8         arrBuffer[constant::buffer::kMax32];
                const uint32 length = StringUtil::formatNumber( arrBuffer, constant::buffer::kMax32, value );
                inoutText.append( arrBuffer, length );
            }

            static void appendRoomID( string& inoutText, int32 roomIndex )
            {
                if ( roomIndex == 0 )
                {
                    inoutText += "entrance";
                    return;
                }
                inoutText += "room";
                appendNumber( inoutText, roomIndex );
            }

            static string makeRoomText( int32 roomIndex )
            {
                string text;
                appendRoomID( text, roomIndex );
                return text;
            }

            static hashed_string makeRoomID( int32 roomIndex )
            {
                const string text = makeRoomText( roomIndex );
                return hashed_string( string_view( text.data(), text.size() ) );
            }

            /** @brief @p pID 가 nullptr 이면 방 번호의 id 입니다. */
            static void writeArea( XMLWriter& writer, const utf8* pID, int32 roomIndex, const utf8* pRegion, int32 x, int32 y )
            {
                writer.beginElement( "Area" );
                writer.attribute( "id", pID != nullptr ? string( pID ) : makeRoomText( roomIndex ) );
                writer.attribute( "region", pRegion );
                writer.attributeInt( "x", x );
                writer.attributeInt( "y", y );
                writer.endElement();
            }

            /** @brief 바깥과 배를 잇는 `Ship` 길입니다. */
            static void writeShipLink( XMLWriter& writer )
            {
                writer.beginElement( "Link" );
                writer.attribute( "from", "outside" );
                writer.attribute( "to", "ship" );
                writer.attribute( "kind", "Ship" );
                writer.endElement();
            }

            /** @brief 방 번호가 음수이면 바깥(`outside`)입니다. */
            static void writeLink( XMLWriter& writer, int32 fromRoom, int32 toRoom, const utf8* pKind, bool bLocked )
            {
                const string toText = makeRoomText( toRoom );
                writer.beginElement( "Link" );
                writer.attribute( "from", fromRoom < 0 ? string( "outside" ) : makeRoomText( fromRoom ) );
                writer.attribute( "to", toText );
                writer.attribute( "kind", pKind );
                if ( bLocked )
                    writer.attribute( "requires", "unlocked." + toText );
                writer.endElement();
            }

            static const ScavengerScrapDef* pickScrap( const ScavengerCatalog& catalog, const ScavengerMoonDef& moon, GameRandom& random )
            {
                // 위성이 목록을 주면 그 안에서, 아니면 카탈로그 전체에서 고른다. 가중치 뽑기 하나(`nextFloat` 한 번)를 같이 쓴다.
                if ( moon._listScrap.empty() )
                {
                    const vector<ScavengerScrapDef>& listScrap = catalog.getScraps();
                    const optional<int32>            index     = random.pickWeightedIndex( listScrap, []( const ScavengerScrapDef& def )
                                   { return def._spawnWeight; } );
                    return index.has_value() ? listScrap.data() + *index : nullptr;
                }
                // 목록의 모르는 이름은 카탈로그가 읽을 때 오류로 알렸다 — 뽑히지 않게 무게 0 으로 친다(내리는 순간 빈 포인터를 읽지 않는다).
                const optional<int32> index = random.pickWeightedIndex( moon._listScrap, [&catalog]( const hashed_string& scrapID )
                {
                    const ScavengerScrapDef* pDef = catalog.findScrap( scrapID );
                    return pDef != nullptr ? pDef->_spawnWeight : 0.0f;
                } );
                return index.has_value() ? catalog.findScrap( moon._listScrap[static_cast<size_t>( *index )] ) : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ScavengerFacility::ScavengerFacility()
        : _graph{}
        , _listGroundScrap{}
        , _listUnlockedFlag{}
        , _layoutMoonID{}
        , _pFlags{ nullptr }
        , _pCatalog{ nullptr }
        , _layoutSeed{ 0u }
        , _nextUid{ 1 }
        , _roomCount{ 0 }
        , _lockedDoorCount{ 0 }
    {
    }

    bool ScavengerFacility::createLayout( const ScavengerCatalog& catalog, const ScavengerMoonDef& moon, uint32 seed, float32 valueScale )
    {
        clear();
        _pCatalog     = &catalog;
        _layoutMoonID = moon._id;
        _layoutSeed   = seed;
        GameRandom                  random{ seed };
        const ScavengerFacilityDef& settings = catalog.getFacilitySettings();
        string                      xml;
        XMLWriter                   writer{ xml };
        writer.beginElement( "AreaGraph" );
        ScavengerFacilityInternal::writeArea( writer, "outside", 0, "Outside", 0, 0 );
        ScavengerFacilityInternal::writeArea( writer, "ship", 0, "Ship", constant::invalid_index::kInt32, 0 );
        if ( moon._bCompany == false )
        {
            _roomCount = random.nextInt( settings._minRooms, settings._maxRooms );
            vector<int32> listDepth( static_cast<size_t>( _roomCount ), 0 );
            vector<int32> listParent( static_cast<size_t>( _roomCount ), constant::invalid_index::kInt32 );
            for ( int32 room = 1; room < _roomCount; ++room )
            {
                listParent[static_cast<size_t>( room )] = random.nextInt( 0, room - 1 );
                listDepth[static_cast<size_t>( room )]  = listDepth[static_cast<size_t>( listParent[static_cast<size_t>( room )] )] + 1;
            }
            for ( int32 room = 0; room < _roomCount; ++room )
            {
                ScavengerFacilityInternal::writeArea( writer, nullptr, room, "Facility", listDepth[static_cast<size_t>( room )] + 1, room );
            }

            ScavengerFacilityInternal::writeShipLink( writer );
            ScavengerFacilityInternal::writeLink( writer, constant::invalid_index::kInt32, 0, "MainEntrance", false );
            for ( int32 room = 1; room < _roomCount; ++room )
            {
                const bool bLocked = random.nextChance( settings._lockedChance );
                _lockedDoorCount += bLocked ? 1 : 0;
                ScavengerFacilityInternal::writeLink( writer, listParent[static_cast<size_t>( room )], room, "Door", bLocked );
            }
            for ( int32 room = 2; room < _roomCount; ++room )
            {
                if ( random.nextChance( settings._loopChance ) == false )
                    continue;
                const int32 other = random.nextInt( 0, room - 1 );
                if ( other != listParent[static_cast<size_t>( room )] )
                    ScavengerFacilityInternal::writeLink( writer, other, room, "Door", false );
            }
            // 화재 출구는 정문에서 가장 깊은 방부터 만든다(같으면 번호 순).
            vector<int32> listExitCandidate;
            for ( int32 room = 1; room < _roomCount; ++room )
            {
                listExitCandidate.push_back( room );
            }
            VectorUtil::stableSort( listExitCandidate, [&]( int32 lhs, int32 rhs )
            { return listDepth[static_cast<size_t>( lhs )] > listDepth[static_cast<size_t>( rhs )]; } );
            const int32 exitCount = MathUtil::min( settings._fireExits, static_cast<int32>( listExitCandidate.size() ) );
            for ( int32 exitIndex = 0; exitIndex < exitCount; ++exitIndex )
            {
                ScavengerFacilityInternal::writeLink( writer, constant::invalid_index::kInt32, listExitCandidate[static_cast<size_t>( exitIndex )], "FireExit", false );
            }
        }
        else
        {
            ScavengerFacilityInternal::writeShipLink( writer );
        }
        writer.endElement();
        SW_ASSERT( writer.isComplete() );
        if ( _graph.loadFromXMLText( string_view( xml.data(), xml.size() ), "ScavengerFacility" ) == false )
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
            scrap._scrapID    = pDef->_id;
            scrap._weight     = pDef->_weight;
            scrap._value      = static_cast<int32>( baseValue * moonScale * MathUtil::max( 0.0f, valueScale ) + 0.5f );
            scrap._bTwoHanded = pDef->_bTwoHanded;
            (void)placeScrap( scrap, ScavengerFacilityInternal::makeRoomID( room ) );
        }
        return true;
    }

    void ScavengerFacility::clear()
    {
        // 빌린 플래그는 나눠 쓴다. 이 시설이 둔 잠금 해제만 지운다.
        for ( const hashed_string& flag : _listUnlockedFlag )
        {
            if ( _pFlags != nullptr )
                (void)_pFlags->clearFlag( flag );
        }
        _listUnlockedFlag.clear();
        _listGroundScrap.clear();
        _roomCount       = 0;
        _lockedDoorCount = 0;
    }

    bool ScavengerFacility::unlockDoor( const hashed_string& roomID )
    {
        string flagText = "unlocked.";
        flagText += roomID.c_str();
        const hashed_string flag( string_view( flagText.data(), flagText.size() ) );
        if ( _pFlags == nullptr || _pFlags->hasFlag( flag ) )
            return false;
        for ( const AreaLink& link : _graph.getLinks() )
        {
            if ( link._to == roomID && link._requires.isEmpty() == false )
            {
                _pFlags->setFlag( flag );
                _listUnlockedFlag.push_back( flag );
                return true;
            }
        }
        return false;
    }

    bool ScavengerFacility::canTraverse( const hashed_string& fromID, const hashed_string& toID ) const
    {
        return _graph.canTraverse( fromID, toID, getFlags() );
    }

    const GameFlags& ScavengerFacility::getFlags() const
    {
        static const GameFlags kNoFlags; // 빌리지 않은 시설이므로 잠긴 문은 닫힌 채이다
        return _pFlags != nullptr ? *_pFlags : kNoFlags;
    }

    int32 ScavengerFacility::placeScrap( const ScavengerScrap& scrap, const hashed_string& areaID )
    {
        ScavengerScrap placed = scrap;
        placed._areaID        = areaID;
        if ( placed._uid <= 0 )
            placed._uid = _nextUid++;
        _listGroundScrap.push_back( placed );
        return placed._uid;
    }

    bool ScavengerFacility::tryTakeScrap( int32 uid, const hashed_string& areaID, ScavengerScrap& outScrap )
    {
        for ( size_t index = 0; index < _listGroundScrap.size(); ++index )
        {
            if ( _listGroundScrap[index]._uid != uid || _listGroundScrap[index]._areaID != areaID )
                continue;
            outScrap         = _listGroundScrap[index];
            outScrap._areaID = hashed_string{};
            _listGroundScrap.erase( _listGroundScrap.begin() + static_cast<ptrdiff_t>( index ) );
            return true;
        }
        return false;
    }

    const ScavengerScrap* ScavengerFacility::findGroundScrap( int32 uid ) const
    {
        return VectorUtil::findBy<&ScavengerScrap::_uid>( _listGroundScrap, uid );
    }

    int32 ScavengerFacility::computeGroundValue() const
    {
        int32 total = 0;
        for ( const ScavengerScrap& scrap : _listGroundScrap )
        {
            total += scrap.isBody() ? 0 : scrap._value;
        }
        return total;
    }

    void ScavengerFacility::serializeState( StateIO& io )
    {
        io.value( _layoutMoonID );
        io.value( _layoutSeed );
        io.list( _listGroundScrap, kMinScrapBytes, [&]( ScavengerScrap& scrap )
        { serializeScrap( io, scrap ); } );
        io.list( _listUnlockedFlag );
        io.value( _nextUid );
        io.value( _roomCount );
        io.value( _lockedDoorCount );
        // 시드 0 은 난수의 고정점이라 만든 지도에는 없다.
        if ( io.check( 1 <= _nextUid && 0 <= _roomCount && 0 <= _lockedDoorCount && ( _layoutMoonID.empty() || _layoutSeed != 0u ) ) == false || io.isWriting() )
            return;
        for ( const ScavengerScrap& scrap : _listGroundScrap )
        {
            // 번호는 `placeScrap` 이 나눠 준다. 다음 번호 이상이면 새로 놓는 고철과 겹쳐 찾기가 엉뚱한 것을 집는다.
            io.check( 0 < scrap._uid && scrap._uid < _nextUid );
        }
        // 그래프는 같은 위성과 같은 시드로 다시 만든다(고철 배치는 버리고 읽은 바닥을 쓴다).
        _graph.clear();
        if ( io.isOk() && _layoutMoonID.empty() == false )
        {
            const ScavengerMoonDef* pMoon = _pCatalog != nullptr ? _pCatalog->findMoon( _layoutMoonID ) : nullptr;
            if ( io.check( pMoon != nullptr ) == false )
                return;
            ScavengerFacility built;
            if ( io.check( built.createLayout( *_pCatalog, *pMoon, _layoutSeed, 1.0f ) ) )
            {
                _graph = std::move( built._graph );
            }
        }
    }

    void ScavengerFacility::serializeScrap( StateIO& io, ScavengerScrap& scrap )
    {
        io.value( scrap._scrapID );
        io.value( scrap._areaID );
        io.value( scrap._weight );
        io.value( scrap._uid );
        io.value( scrap._value );
        io.value( scrap._bodyOf );
        io.value( scrap._day );
        io.flag( scrap._bTwoHanded );
        // 무게가 NaN 이면 이동 속도가, 음수 가치면 매입액이 깨진다.
        io.check( 0.0f <= scrap._weight && 0 <= scrap._value && -1 <= scrap._bodyOf );
    }

} // namespace sw
