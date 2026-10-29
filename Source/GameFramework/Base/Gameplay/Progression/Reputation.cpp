#include "pch.h"

#include "GameFramework/Base/Gameplay/Progression/Reputation.h"

#include "Core/Container/CompareUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "Reputation" );

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
    uint32 ReputationCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Faction" ); node; node = node.findNextSibling( "Faction" ) )
        {
            const utf8* pID = XMLReadUtil::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            FactionDef faction;
            faction._id       = hashed_string( pID );
            const utf8* pName = node.findAttribute( "name" );
            faction._name     = pName != nullptr ? pName : pID;
            // 적지 않은 칸은 FactionDef 의 초기값이다. 깨진 값 · 범위 밖 값은 그 세력을 넣지 않는 오류다(자르지 않는다).
            constexpr int32 kAnyInt = MathUtil::kMaxInt32;
            bool            bValid  = XMLReadUtil::readOptionalNumber( node, "min", sourceName, MathUtil::kMinInt32, kAnyInt, faction._minValue );
            bValid                  = XMLReadUtil::readOptionalNumber( node, "max", sourceName, faction._minValue, kAnyInt, faction._maxValue ) && bValid;
            bValid                  = XMLReadUtil::readOptionalNumber( node, "start", sourceName, faction._minValue, faction._maxValue, faction._startValue ) && bValid;
            bValid                  = XMLReadUtil::readOptionalNumber( node, "decay", sourceName, 0, kAnyInt, faction._dailyDecay ) && bValid;
            for ( XMLNode child = node.findChild( "Tier" ); child; child = child.findNextSibling( "Tier" ) )
            {
                // 단계의 하한을 적지 않으면 세력의 하한이다(이 데이터의 값).
                ReputationTier tier{ hashed_string{}, faction._minValue };
                const bool     bNamed = XMLReadUtil::readRequiredName( child, "name", sourceName, tier._name );
                bValid                = XMLReadUtil::readOptionalNumber( child, "min", sourceName, faction._minValue, faction._maxValue, tier._minValue ) && bValid;
                if ( bNamed )
                {
                    faction._listTier.push_back( tier );
                }
                else
                {
                    SW_LOG_ERROR( "%#: faction '%#' has a <Tier> without a name", sourceName, pID );
                    bValid = false;
                }
            }
            VectorUtil::stableSort( faction._listTier, Ascending<&ReputationTier::_minValue>{} );
            for ( XMLNode child = node.findChild( "Link" ); child; child = child.findNextSibling( "Link" ) )
            {
                // 연결은 상대 세력과 비율을 반드시 적는다 — 빠진 비율을 0 으로 두지 않는다.
                const std::optional<string>  linkedFaction = child.readRequiredAttribute<string>( "faction" );
                const std::optional<float32> ratio         = child.readRequiredAttribute<float32>( "ratio" );
                if ( linkedFaction.has_value() && ratio.has_value() )
                    faction._listLink.push_back( ReputationLink{ hashed_string( *linkedFaction ), *ratio } );
                else
                    bValid = false;
            }
            if ( bValid == false )
            {
                SW_LOG_ERROR( "%#: faction '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            add( faction );
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

    ReputationState::Entry& ReputationState::acquireEntry( const hashed_string& factionID )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._factionID == factionID )
                return entry;
        }
        const FactionDef* pFaction = _pCatalog != nullptr ? _pCatalog->find( factionID ) : nullptr;
        _listEntry.push_back( Entry{ factionID, pFaction != nullptr ? pFaction->_startValue : 0 } );
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

    int32 ReputationState::applyDelta( const hashed_string& factionID, int32 delta )
    {
        const FactionDef* pFaction = _pCatalog != nullptr ? _pCatalog->find( factionID ) : nullptr;
        Entry&            entry    = acquireEntry( factionID );
        const int32       minValue = pFaction != nullptr ? pFaction->_minValue : ReputationInternal::kUnknownMin;
        const int32       maxValue = pFaction != nullptr ? pFaction->_maxValue : ReputationInternal::kUnknownMax;
        const int32       oldValue = entry._value;
        const int32       oldTier  = computeTierIndex( pFaction, oldValue );
        entry._value               = MathUtil::clamp( oldValue + delta, minValue, maxValue );
        const int32 newTier        = computeTierIndex( pFaction, entry._value );
        if ( newTier != oldTier )
        {
            ReputationEvent event;
            event._factionID = factionID;
            event._oldTier   = oldTier >= 0 ? pFaction->_listTier[static_cast<size_t>( oldTier )]._name : hashed_string{};
            event._newTier   = newTier >= 0 ? pFaction->_listTier[static_cast<size_t>( newTier )]._name : hashed_string{};
            event._value     = entry._value;
            _eventBuffer.push( event );
        }
        return entry._value - oldValue;
    }

    int32 ReputationState::changeValue( const hashed_string& factionID, int32 delta )
    {
        const int32       changed  = applyDelta( factionID, delta );
        const FactionDef* pFaction = _pCatalog != nullptr ? _pCatalog->find( factionID ) : nullptr;
        if ( pFaction != nullptr && changed != 0 )
        {
            for ( const ReputationLink& link : pFaction->_listLink )
            {
                const float32 linked = static_cast<float32>( changed ) * link._ratio;
                (void)applyDelta( link._factionID, static_cast<int32>( linked < 0.0f ? linked - 0.5f : linked + 0.5f ) ); // 연결 세력의 바뀐 양은 쓰지 않는다
            }
        }
        return changed;
    }

    // 바뀐 양은 쓰지 않는다. 범위 밖이면 경계에서 멈춘다
    void ReputationState::setValue( const hashed_string& factionID, int32 value )
    {
        (void)applyDelta( factionID, value - getValue( factionID ) ); // 바뀐 양은 쓰지 않는다. 범위 밖이면 경계에서 멈춘다
    }

    void ReputationState::advanceDay()
    {
        if ( _pCatalog == nullptr )
            return;
        for ( const FactionDef& faction : _pCatalog->getAll() )
        {
            if ( faction._dailyDecay <= 0 )
                continue;
            const int32 value = getValue( faction._id );
            const int32 gap   = faction._startValue - value;
            if ( gap != 0 )
                (void)applyDelta( faction._id, MathUtil::clamp( gap, -faction._dailyDecay, faction._dailyDecay ) ); // 바뀐 양은 쓰지 않는다
        }
    }

    int32 ReputationState::getValue( const hashed_string& factionID ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._factionID == factionID )
                return entry._value;
        }
        const FactionDef* pFaction = _pCatalog != nullptr ? _pCatalog->find( factionID ) : nullptr;
        return pFaction != nullptr ? pFaction->_startValue : 0;
    }

    int32 ReputationState::getTierIndex( const hashed_string& factionID ) const
    {
        return computeTierIndex( _pCatalog != nullptr ? _pCatalog->find( factionID ) : nullptr, getValue( factionID ) );
    }

    hashed_string ReputationState::getTierName( const hashed_string& factionID ) const
    {
        const FactionDef* pFaction  = _pCatalog != nullptr ? _pCatalog->find( factionID ) : nullptr;
        const int32       tierIndex = computeTierIndex( pFaction, getValue( factionID ) );
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
            StateArchiveUtil::writeName( outArchive, entry._factionID );
            outArchive << entry._value;
        }
    }

    bool ReputationState::readState( Archive& archive )
    {
        uint32 count = 0;
        // 세력마다 이름 길이(4) + 값(4) 이상
        if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        vector<Entry> listEntry;
        listEntry.reserve( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            Entry entry;
            if ( StateArchiveUtil::readName( archive, entry._factionID ) == false )
                return false;
            archive >> entry._value;
            if ( archive.isError() || entry._factionID.empty() )
                return false;
            listEntry.push_back( entry );
        }
        _listEntry = std::move( listEntry );
        _eventBuffer.clear();
        return true;
    }
} // namespace sw

namespace sw
{
    /** @brief `ReputationCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( Friendship, ReputationCatalog::kDataKind, "game/", nullptr, "/data/friendship.xml", &AssetDataKindUtil::loadCatalog<ReputationCatalog> );
} // namespace sw
