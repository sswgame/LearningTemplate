#include "pch.h"

#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerFacility.h"

#include "Core/Common/Defines.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerCatalog.h"

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
                    inoutText += "\" requires=\"unlocked.";
                    appendRoomId( inoutText, toRoom );
                }
                inoutText += "\"/>";
            }

            static const ScavengerScrapDef* pickScrap( const ScavengerCatalog& catalog, const ScavengerMoonDef& moon, GameRandom& random )
            {
                // 달이 목록을 주면 그 안에서, 아니면 카탈로그 전체에서 — 가중치 뽑기 하나(`nextFloat` 한 번)를 같이 쓴다.
                if ( moon._listScrap.empty() )
                {
                    const vector<ScavengerScrapDef>& listScrap = catalog.getScraps();
                    const int32                      index     = random.pickWeightedIndex( listScrap, []( const ScavengerScrapDef& def )
                                             { return def._spawnWeight; } );
                    return index >= 0 ? listScrap.data() + index : nullptr;
                }
                const int32 index = random.pickWeightedIndex( moon._listScrap, [&catalog]( const hashed_string& scrapId )
                { return catalog.findScrap( scrapId )->_spawnWeight; } );
                return index >= 0 ? catalog.findScrap( moon._listScrap[static_cast<size_t>( index )] ) : nullptr;
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
        , _layoutMoonId{}
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
        _layoutMoonId = moon._id;
        _layoutSeed   = seed;
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
            {
                ScavengerFacilityInternal::appendArea( xml, nullptr, room, "Facility", listDepth[static_cast<size_t>( room )] + 1, room );
            }

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
            {
                listExitCandidate.push_back( room );
            }
            std::stable_sort( listExitCandidate.begin(), listExitCandidate.end(),
                              [&]( int32 lhs, int32 rhs )
            { return listDepth[static_cast<size_t>( lhs )] > listDepth[static_cast<size_t>( rhs )]; } );
            const int32 exitCount = MathUtil::min( settings._fireExits, static_cast<int32>( listExitCandidate.size() ) );
            for ( int32 exitIndex = 0; exitIndex < exitCount; ++exitIndex )
            {
                ScavengerFacilityInternal::appendLink( xml, -1, listExitCandidate[static_cast<size_t>( exitIndex )], "FireExit", false );
            }
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
        // 빌린 플래그는 나눠 쓴다 — 이 시설이 둔 잠금 해제만 지운다.
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

    bool ScavengerFacility::unlockDoor( const hashed_string& roomId )
    {
        string flagText = "unlocked.";
        flagText += roomId.c_str();
        const hashed_string flag( string_view( flagText.data(), flagText.size() ) );
        if ( _pFlags == nullptr || _pFlags->hasFlag( flag ) )
            return false;
        for ( const AreaLink& link : _graph.getLinks() )
        {
            if ( link._to == roomId && link._requires.empty() == false )
            {
                _pFlags->setFlag( flag );
                _listUnlockedFlag.push_back( flag );
                return true;
            }
        }
        return false;
    }

    bool ScavengerFacility::canTraverse( const hashed_string& fromId, const hashed_string& toId ) const { return _graph.canTraverse( fromId, toId, getFlags() ); }

    const GameFlags& ScavengerFacility::getFlags() const
    {
        static const GameFlags kNoFlags; // 빌리지 않은 시설 — 잠긴 문은 닫힌 채다
        return _pFlags != nullptr ? *_pFlags : kNoFlags;
    }

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
        {
            total += scrap.isBody() ? 0 : scrap._value;
        }
        return total;
    }

    void ScavengerFacility::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _layoutMoonId );
        outArchive << _layoutSeed;
        outArchive << static_cast<uint32>( _listGroundScrap.size() );
        for ( const ScavengerScrap& scrap : _listGroundScrap )
        {
            writeScrap( outArchive, scrap );
        }
        outArchive << static_cast<uint32>( _listUnlockedFlag.size() );
        for ( const hashed_string& flag : _listUnlockedFlag )
        {
            StateArchiveUtil::writeName( outArchive, flag );
        }
        outArchive << _nextUid;
        outArchive << _roomCount;
        outArchive << _lockedDoorCount;
    }

    bool ScavengerFacility::readState( Archive& archive )
    {
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 빌린 플래그 포인터는 사본이 그대로 든다.
        ScavengerFacility restored = *this;
        if ( StateArchiveUtil::readName( archive, restored._layoutMoonId ) == false )
            return false;
        archive >> restored._layoutSeed;

        uint32 scrapCount = 0;
        if ( StateArchiveUtil::readCount( archive, kMinScrapBytes, scrapCount ) == false )
            return false;
        restored._listGroundScrap.assign( scrapCount, ScavengerScrap{} );
        for ( ScavengerScrap& scrap : restored._listGroundScrap )
        {
            if ( readScrap( archive, scrap ) == false )
                return false;
        }

        uint32 flagCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, flagCount ) == false )
            return false;
        restored._listUnlockedFlag.assign( flagCount, hashed_string{} );
        for ( hashed_string& flag : restored._listUnlockedFlag )
        {
            if ( StateArchiveUtil::readName( archive, flag ) == false )
                return false;
        }
        archive >> restored._nextUid;
        archive >> restored._roomCount;
        archive >> restored._lockedDoorCount;
        const bool bValid = archive.isOk() && 1 <= restored._nextUid && 0 <= restored._roomCount && 0 <= restored._lockedDoorCount;
        if ( bValid == false )
            return false;

        // 그래프는 같은 위성 · 같은 씨앗으로 다시 짓는다(고철 배치는 버리고 읽은 바닥을 쓴다).
        restored._graph.clear();
        if ( restored._layoutMoonId.empty() == false )
        {
            const ScavengerMoonDef* pMoon = _pCatalog != nullptr ? _pCatalog->findMoon( restored._layoutMoonId ) : nullptr;
            if ( pMoon == nullptr )
                return false;
            ScavengerFacility built;
            if ( built.createLayout( *_pCatalog, *pMoon, restored._layoutSeed, 1.0f ) == false )
                return false;
            restored._graph = std::move( built._graph );
        }
        *this = std::move( restored );
        return true;
    }

    void ScavengerFacility::writeScrap( Archive& outArchive, const ScavengerScrap& scrap )
    {
        StateArchiveUtil::writeName( outArchive, scrap._scrapId );
        StateArchiveUtil::writeName( outArchive, scrap._areaId );
        outArchive << scrap._weight;
        outArchive << scrap._uid;
        outArchive << scrap._value;
        outArchive << scrap._bodyOf;
        outArchive << scrap._day;
        outArchive << scrap._bTwoHanded;
    }

    bool ScavengerFacility::readScrap( Archive& archive, ScavengerScrap& outScrap )
    {
        ScavengerScrap scrap;
        if ( StateArchiveUtil::readName( archive, scrap._scrapId ) == false || StateArchiveUtil::readName( archive, scrap._areaId ) == false )
            return false;
        archive >> scrap._weight;
        archive >> scrap._uid;
        archive >> scrap._value;
        archive >> scrap._bodyOf;
        archive >> scrap._day;
        archive >> scrap._bTwoHanded;
        const bool bValid = archive.isOk() && -1 <= scrap._bodyOf && scrap._bTwoHanded <= SW_TRUE;
        if ( bValid == false )
            return false;
        outScrap = scrap;
        return true;
    }
} // namespace sw
