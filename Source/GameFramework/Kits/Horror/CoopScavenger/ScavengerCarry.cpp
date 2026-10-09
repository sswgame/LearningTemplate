#include "pch.h"

#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerCarry.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    ScavengerCarry::ScavengerCarry()
        : _listScrap{}
        , _settings{}
    {
    }

    void ScavengerCarry::initialize( const ScavengerCarrySettings& settings )
    {
        _settings = settings;
        _listScrap.clear();
    }

    ScavengerPickupResult ScavengerCarry::evaluatePickup() const
    {
        if ( isHoldingTwoHanded() )
            return ScavengerPickupResult::HandsFull;
        if ( static_cast<int32>( _listScrap.size() ) >= _settings._slotCount )
            return ScavengerPickupResult::SlotsFull;
        return ScavengerPickupResult::Ok;
    }

    ScavengerPickupResult ScavengerCarry::add( const ScavengerScrap& scrap )
    {
        const ScavengerPickupResult result = evaluatePickup();
        if ( result == ScavengerPickupResult::Ok )
            _listScrap.push_back( scrap );
        return result;
    }

    bool ScavengerCarry::tryRemove( int32 uid, ScavengerScrap& outScrap )
    {
        for ( size_t index = 0; index < _listScrap.size(); ++index )
        {
            if ( _listScrap[index]._uid != uid )
                continue;
            outScrap = _listScrap[index];
            _listScrap.erase( _listScrap.begin() + static_cast<ptrdiff_t>( index ) );
            return true;
        }
        return false;
    }

    void ScavengerCarry::takeAll( vector<ScavengerScrap>& outListScrap )
    {
        outListScrap.insert( outListScrap.end(), _listScrap.begin(), _listScrap.end() );
        _listScrap.clear();
    }

    float32 ScavengerCarry::computeWeight() const
    {
        float32 total = 0.0f;
        for ( const ScavengerScrap& scrap : _listScrap )
        {
            total += scrap._weight;
        }
        return total;
    }

    float32 ScavengerCarry::computeSpeedScale() const { return MathUtil::max( _settings._minSpeedScale, 1.0f - computeWeight() * _settings._speedPerWeight ); }

    int32 ScavengerCarry::computeValue() const
    {
        int32 total = 0;
        for ( const ScavengerScrap& scrap : _listScrap )
        {
            total += scrap._value;
        }
        return total;
    }

    bool ScavengerCarry::isHoldingTwoHanded() const
    {
        for ( const ScavengerScrap& scrap : _listScrap )
        {
            if ( scrap._bTwoHanded == SW_TRUE )
                return true;
        }
        return false;
    }

    void ScavengerCarry::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listScrap.size() );
        for ( const ScavengerScrap& scrap : _listScrap )
        {
            ScavengerFacility::writeScrap( outArchive, scrap );
        }
    }

    bool ScavengerCarry::readState( Archive& archive )
    {
        uint32 count = 0;
        if ( StateArchiveUtil::readCount( archive, ScavengerFacility::kMinScrapBytes, count ) == false || static_cast<int32>( count ) > _settings._slotCount )
            return false;
        vector<ScavengerScrap> listScrap( count );
        for ( ScavengerScrap& scrap : listScrap )
        {
            if ( ScavengerFacility::readScrap( archive, scrap ) == false )
                return false;
        }
        _listScrap = std::move( listScrap );
        return true;
    }
} // namespace sw
