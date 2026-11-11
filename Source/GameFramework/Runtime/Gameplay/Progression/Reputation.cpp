#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"

#include "Core/Algorithm/CompareUtil.h"
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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
    uint32 ReputationCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<ReputationCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&ReputationCatalog::loadFaction>( "Faction" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool ReputationCatalog::loadFaction( const XMLNode& node, string_view sourceName )
    {

        // 적지 않은 칸은 FactionDef 의 초기값이다. 깨진 값 · 범위 밖 값(최대 >= 최소, 시작은 그 사이)은 그 세력을 넣지 않는 오류다(자르지 않는다).
        FactionDef faction;
        bool       bValid = XMLFields::readProperties( node, faction, sourceName );
        if ( faction._id.empty() )
            return false;
        const utf8* const pID = faction._id.c_str();
        if ( faction._name.empty() )
            faction._name = pID;
        for ( XMLNode child = node.findChild( "Tier" ); child; child = child.findNextSibling( "Tier" ) )
        {
            // 단계의 하한을 적지 않으면 세력의 하한이다(이 데이터의 값).
            ReputationTier tier{ hashed_string{}, faction._minValue };
            const bool     bTierRead = XMLFields::readProperties( child, tier, sourceName ); // 이름 필수
            const bool     bInside   = faction._minValue <= tier._minValue && tier._minValue <= faction._maxValue;
            if ( bTierRead && bInside )
            {
                faction._listTier.push_back( tier );
            }
            else
            {
                SW_LOG_ERROR_ID( "Reputation.FactionTierInvalid", "%#: faction '%#' has a <Tier> without a name or with min outside [min, max]", sourceName, pID );
                bValid = false;
            }
        }
        VectorUtil::stableSort( faction._listTier, Ascending<&ReputationTier::_minValue>{} );
        for ( XMLNode child = node.findChild( "Link" ); child; child = child.findNextSibling( "Link" ) )
        {
            // 연결은 상대 세력과 비율을 반드시 적는다 — 빠진 비율을 0 으로 두지 않는다.
            ReputationLink link;
            if ( XMLFields::readProperties( child, link, sourceName ) )
                faction._listLink.push_back( link );
            else
                bValid = false;
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "Reputation.FactionInvalidFieldsSkipped", "%#: faction '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        add( faction );
        return true;
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
            return constant::invalid_index::kInt32;
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

    void ReputationState::pollEvents( vector<ReputationEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void ReputationState::serializeState( StateIO& io )
    {
        // 세력마다 이름 길이(4) + 값(4) 이상
        io.list( _listEntry, 8, [&]( Entry& entry )
        {
            io.value( entry._factionID );
            io.value( entry._value );
            io.check( entry._factionID.empty() == false );
        } );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
        }
    }

} // namespace sw

namespace sw
{
    /** @brief `ReputationCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Friendship, ReputationCatalog, SW_GF_API, AssetTypeDef::makeDataFile( ReputationCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/friendship.xml" }, AssetTypeLoader::of<ReputationCatalog>( MemoryTag::Game ) ) );
} // namespace sw
