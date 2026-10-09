#include "pch.h"

#include "GameFramework/Base/Gameplay/Progression/Reputation.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ReputationInternal
        {
            static constexpr int32 kUnknownMin = 0;
            static constexpr int32 kUnknownMax = 1000;
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 ReputationCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Faction" ); node; node = node.findNextSibling( "Faction" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            FactionDef faction;
            faction._id         = hashed_string( pId );
            const utf8* pName   = node.findAttribute( "name" );
            faction._name       = pName != nullptr ? pName : pId;
            faction._minValue   = node.getAttributeInt( "min", faction._minValue );
            faction._maxValue   = MathUtil::max( faction._minValue, node.getAttributeInt( "max", faction._maxValue ) );
            faction._startValue = MathUtil::clamp( node.getAttributeInt( "start", faction._startValue ), faction._minValue, faction._maxValue );
            faction._dailyDecay = MathUtil::max( 0, node.getAttributeInt( "decay", faction._dailyDecay ) );
            for ( XmlNode child = node.findChild( "Tier" ); child; child = child.findNextSibling( "Tier" ) )
            {
                const utf8* pTierName = child.findAttribute( "name" );
                if ( pTierName != nullptr )
                    faction._listTier.push_back( ReputationTier{ hashed_string( pTierName ), child.getAttributeInt( "min", faction._minValue ) } );
            }
            std::stable_sort( faction._listTier.begin(), faction._listTier.end(),
                              []( const ReputationTier& lhs, const ReputationTier& rhs )
            { return lhs._minValue < rhs._minValue; } );
            for ( XmlNode child = node.findChild( "Link" ); child; child = child.findNextSibling( "Link" ) )
            {
                const utf8* pFaction = child.findAttribute( "faction" );
                if ( pFaction != nullptr )
                    faction._listLink.push_back( ReputationLink{ hashed_string( pFaction ), child.getAttributeFloat( "ratio", 0.0f ) } );
            }
            addFaction( faction );
            ++loadedCount;
        }
        return loadedCount;
    }

    ReputationState::ReputationState()
        : _listEntry{}
        , _eventBuffer{}
        , _pCatalog{ nullptr }
    {
    }

    void ReputationState::initialize( const ReputationCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listEntry.clear();
        _eventBuffer.clear();
    }

    ReputationState::Entry& ReputationState::acquireEntry( const hashed_string& factionId )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._factionId == factionId )
                return entry;
        }
        const FactionDef* pFaction = _pCatalog != nullptr ? _pCatalog->findFaction( factionId ) : nullptr;
        _listEntry.push_back( Entry{ factionId, pFaction != nullptr ? pFaction->_startValue : 0 } );
        return _listEntry.back();
    }

    int32 ReputationState::computeTierIndex( const FactionDef* pFaction, int32 value )
    {
        if ( pFaction == nullptr || pFaction->_listTier.empty() )
            return -1;
        int32 tierIndex = 0;
        for ( size_t index = 0; index < pFaction->_listTier.size(); ++index )
        {
            if ( value >= pFaction->_listTier[index]._minValue )
                tierIndex = static_cast<int32>( index );
        }
        return tierIndex;
    }

    int32 ReputationState::applyDelta( const hashed_string& factionId, int32 delta )
    {
        const FactionDef* pFaction = _pCatalog != nullptr ? _pCatalog->findFaction( factionId ) : nullptr;
        Entry&            entry    = acquireEntry( factionId );
        const int32       minValue = pFaction != nullptr ? pFaction->_minValue : ReputationInternal::kUnknownMin;
        const int32       maxValue = pFaction != nullptr ? pFaction->_maxValue : ReputationInternal::kUnknownMax;
        const int32       oldValue = entry._value;
        const int32       oldTier  = computeTierIndex( pFaction, oldValue );
        entry._value               = MathUtil::clamp( oldValue + delta, minValue, maxValue );
        const int32 newTier        = computeTierIndex( pFaction, entry._value );
        if ( newTier != oldTier )
        {
            ReputationEvent event;
            event._factionId = factionId;
            event._oldTier   = oldTier >= 0 ? pFaction->_listTier[static_cast<size_t>( oldTier )]._name : hashed_string{};
            event._newTier   = newTier >= 0 ? pFaction->_listTier[static_cast<size_t>( newTier )]._name : hashed_string{};
            event._value     = entry._value;
            _eventBuffer.push( event );
        }
        return entry._value - oldValue;
    }

    int32 ReputationState::changeValue( const hashed_string& factionId, int32 delta )
    {
        const int32       changed  = applyDelta( factionId, delta );
        const FactionDef* pFaction = _pCatalog != nullptr ? _pCatalog->findFaction( factionId ) : nullptr;
        if ( pFaction != nullptr && changed != 0 )
        {
            for ( const ReputationLink& link : pFaction->_listLink )
            {
                const float32 linked = static_cast<float32>( changed ) * link._ratio;
                (void)applyDelta( link._factionId, static_cast<int32>( linked < 0.0f ? linked - 0.5f : linked + 0.5f ) ); // 연결 세력의 바뀐 양은 쓰지 않는다
            }
        }
        return changed;
    }

    // 바뀐 양은 쓰지 않는다 — 범위 밖이면 경계에서 멈춘다
    void ReputationState::setValue( const hashed_string& factionId, int32 value ) { (void)applyDelta( factionId, value - getValue( factionId ) ); }

    void ReputationState::advanceDay()
    {
        if ( _pCatalog == nullptr )
            return;
        for ( const FactionDef& faction : _pCatalog->getFactions() )
        {
            if ( faction._dailyDecay <= 0 )
                continue;
            const int32 value = getValue( faction._id );
            const int32 gap   = faction._startValue - value;
            if ( gap != 0 )
                (void)applyDelta( faction._id, MathUtil::clamp( gap, -faction._dailyDecay, faction._dailyDecay ) ); // 바뀐 양은 쓰지 않는다
        }
    }

    int32 ReputationState::getValue( const hashed_string& factionId ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._factionId == factionId )
                return entry._value;
        }
        const FactionDef* pFaction = _pCatalog != nullptr ? _pCatalog->findFaction( factionId ) : nullptr;
        return pFaction != nullptr ? pFaction->_startValue : 0;
    }

    int32 ReputationState::getTierIndex( const hashed_string& factionId ) const
    {
        return computeTierIndex( _pCatalog != nullptr ? _pCatalog->findFaction( factionId ) : nullptr, getValue( factionId ) );
    }

    hashed_string ReputationState::getTierName( const hashed_string& factionId ) const
    {
        const FactionDef* pFaction  = _pCatalog != nullptr ? _pCatalog->findFaction( factionId ) : nullptr;
        const int32       tierIndex = computeTierIndex( pFaction, getValue( factionId ) );
        return tierIndex >= 0 ? pFaction->_listTier[static_cast<size_t>( tierIndex )]._name : hashed_string{};
    }

    void ReputationState::drainEvents( vector<ReputationEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void ReputationState::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listEntry.size() );
        for ( const Entry& entry : _listEntry )
        {
            StateArchiveUtil::writeName( outArchive, entry._factionId );
            outArchive << entry._value;
        }
    }

    bool ReputationState::readState( Archive& archive )
    {
        uint32 count = 0;
        // 칸마다 이름 길이(4) + 값(4) 이상
        if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        vector<Entry> listEntry;
        listEntry.reserve( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            Entry entry;
            if ( StateArchiveUtil::readName( archive, entry._factionId ) == false )
                return false;
            archive >> entry._value;
            if ( archive.isError() || entry._factionId.empty() )
                return false;
            listEntry.push_back( entry );
        }
        _listEntry = std::move( listEntry );
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
