#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    namespace
    {
        struct CityBuilderGameModeComponentInternal
        {
            static constexpr float32 kPercent = 100.0f; ///< 비율 → 백분율(로그)
        };
    } // namespace
} // namespace sw

namespace sw
{
    CityBuilderGameModeComponent::CityBuilderGameModeComponent()
        : _cityDataPath{}
        , _startingMoney{ 0 }
        , _speedRange{}
        , _pCatalog{}
        , _city{}
        , _cityEventLease{}
        , _listStepEvent{}
        , _speed{}
        , _layoutRevision{ 0 }
        , _buildCount{}
        , _demolishCount{}
    {
    }

    CityBuilderGameModeComponent::~CityBuilderGameModeComponent() = default;

    CityPlaceResult CityBuilderGameModeComponent::submitPlaceRoad( const int2& tile )
    {
        if ( isStarted() == false )
            return CityPlaceResult::OutOfBounds;
        const CityPlaceResult result = _city.placeRoad( tile._x, tile._y );
        if ( result != CityPlaceResult::Ok )
            return result;
        _buildCount.add();
        markLayoutChanged();
        return result;
    }

    CityPlaceResult CityBuilderGameModeComponent::submitPlaceBuilding( const hashed_string& buildingID, const int2& tile )
    {
        if ( isStarted() == false )
            return CityPlaceResult::OutOfBounds;
        const CityPlaceResult result = _city.placeBuilding( buildingID, tile._x, tile._y );
        if ( result != CityPlaceResult::Ok )
            return result;
        _buildCount.add();
        markLayoutChanged();
        return result;
    }

    bool CityBuilderGameModeComponent::submitDemolish( const int2& tile )
    {
        if ( isStarted() == false || _city.demolish( tile._x, tile._y ) == false )
            return false;
        _demolishCount.add();
        markLayoutChanged();
        return true;
    }

    bool CityBuilderGameModeComponent::submitTimeInput( bool bPause, bool bFaster, bool bSlower )
    {
        if ( isStarted() == false )
            return false;
        _speed.applyInput( bPause, bFaster, bSlower );
        return true;
    }

    void CityBuilderGameModeComponent::logStatus() const
    {
        constexpr float32                    kPercent    = CityBuilderGameModeComponentInternal::kPercent;
        [[maybe_unused]] const Wallet* const pCityWallet = findCityWallet();
        SW_LOG_INFO_ID( "City.Status", "[City] pop %# · money $%# · workers %#/%# · avg house level %# · culture %#%% · fertility %#%%", _city.getPopulation(),
                        pCityWallet != nullptr ? pCityWallet->getBalance( _city.getCurrency() ) : 0, _city.getEmployed(), _city.getWorkforce(), _city.computeAverageHouseLevel(),
                        static_cast<int32>( _city.computeCultureCoverage() * kPercent ), static_cast<int32>( _city.getFloodFertility() * kPercent ) );
    }

    bool CityBuilderGameModeComponent::acquireCatalog()
    {
        return acquireRequired( _cityDataPath, _pCatalog ) && _speed.initialize( _speedRange ); // 까닭은 남았다
    }

    bool CityBuilderGameModeComponent::beginCity()
    {
        if ( _city.claimEvents( _cityEventLease ) == false )
            return false; // 이 디렉터의 표뿐이라 일어날 수 없다 — 둘째 소비자가 생기면 게이트가 어서션을 남긴다
        _listStepEvent.clear();
        _buildCount.reset();
        _demolishCount.reset();
        markLayoutChanged();
        return true;
    }

    void CityBuilderGameModeComponent::collectStepEvents()
    {
        _listStepEvent.clear(); // pollEvents 는 뒤에 붙인다
        _city.pollEvents( _cityEventLease, _listStepEvent );
    }
} // namespace sw
