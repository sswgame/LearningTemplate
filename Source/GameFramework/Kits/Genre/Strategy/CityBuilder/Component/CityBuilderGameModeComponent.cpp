#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    namespace
    {
        struct CityBuilderGameModeComponentInternal
        {
            static constexpr float32 kPercent = 100.0f; ///< 비율 → 백분율(로그)

            /** @brief 감시 값 `City.Money` 입니다. 금고가 없으면 값이 없습니다. */
            [[nodiscard]] static bool readMoney( const CityBuilderGameModeComponent& gameMode, float64& outValue )
            {
                const Wallet* const pCityWallet = gameMode.findCityWallet();
                if ( pCityWallet == nullptr )
                    return false;
                outValue = static_cast<float64>( pCityWallet->getBalance( gameMode.getCity().getCurrency() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( cityPopulation, "City.Population", "Population of the city", CityBuilderGameModeComponent, &CityBuilderGameModeComponent::getPopulation );
    SW_AUTOMATION_COMPONENT_WATCH( cityMoney, "City.Money", "Money in the city treasury", CityBuilderGameModeComponent, &CityBuilderGameModeComponentInternal::readMoney );
    SW_AUTOMATION_GETTER_WATCH( cityBuildCount, "City.BuildCount", "Roads laid and buildings placed by commands this game (successes only)", CityBuilderGameModeComponent,
                                &CityBuilderGameModeComponent::getBuildCount );
    SW_AUTOMATION_GETTER_WATCH( cityDemolishCount, "City.DemolishCount", "Tiles demolished by commands this game", CityBuilderGameModeComponent, &CityBuilderGameModeComponent::getDemolishCount );
} // namespace sw

namespace sw
{
    CityBuilderGameModeComponent::CityBuilderGameModeComponent()
        : _cityDataPath{}
        , _startingMoney{ 0 }
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
        return acquireRequired( _cityDataPath, _pCatalog );
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
