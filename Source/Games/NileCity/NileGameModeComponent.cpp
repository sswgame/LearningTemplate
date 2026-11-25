#include "pch.h"

#include "Games/NileCity/NileGameModeComponent.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct NileGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "NILE" };
            static constexpr float32   kPercent = 100.0f; ///< 비율 → 백분율(로그)
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( nileMonth, "Nile.Month", "Months the city has run", NileGameModeComponent, &NileGameModeComponent::getMonthCount );
} // namespace sw

namespace sw
{
    NileGameModeComponent::NileGameModeComponent()
        : _autoPlanPath{}
        , _secondsPerMonth{ 20.0f }
        , _tileMapPath{}
        , _listTerrain{}
        , _roadX{ 17 }
        , _farmX{ 14 }
        , _secondBlockPeople{ 60 }
        , _thirdBlockPeople{ 160 }
        , _wallet{}
        , _clock{}
        , _planner{}
        , _listClockEvent{}
        , _monthCount{ 0 }
        , _evolvedCount{ 0 }
        , _bAutoPlanToggle{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    NileGameModeComponent::~NileGameModeComponent() = default;

    void NileGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "NileGameMode.CityFoundedNileWASD", "[Nile] the city is founded on the Nile - WASD pan, wheel zoom, Q/E pick building, R road, left click build, right click demolish, "
                                                            "P auto plan, Space pause, - = speed, F1 status" );
    }

    void NileGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime > 0.0f )
        {
            if ( isAutoPlanOn() && _planner.advance( getMutableCity(), _wallet, findCatalog()->getRoadCost() ) > 0 )
                markLayoutChanged();
            if ( getSpeed().isPaused() == false )
            {
                // 시계는 도시가 진행한 시간으로 민다 — 따로 곱하면 프레임 상한에 잘린 도시와 어긋난다(감사 K-6). 배속은 디렉터가 쥔다(시계 배율은 1).
                _clock.update( getMutableCity().update( deltaTime, getSpeed().getTimeScale() ) );
                settleMonths();
            }
        }
        collectStepEvents();
        logCityEvents();
    }

    bool NileGameModeComponent::submitAutoPlanToggle()
    {
        if ( isStarted() == false )
            return false;
        _bAutoPlanToggle = _bAutoPlanToggle == SW_TRUE ? SW_FALSE : SW_TRUE;
        SW_LOG_INFO_ID( "NileGameMode.AutoPlanStep", "[Nile] auto plan %# (step %# of %#)", _bAutoPlanToggle == SW_TRUE ? "on" : "off", _planner.getNextStep(), _planner.getStepCount() );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 데이터
    // ------------------------------------------------------------------------------
    bool NileGameModeComponent::startGame()
    {
        if ( acquireCatalog() == false )
            return false;
        if ( _clock.initialize( makeClockSettings() ) == false )
            return false; // 범위 밖(달 길이 `_secondsPerMonth` 등)은 시계가 오류를 남겼다
        _wallet.clear();
        _wallet.add( findCatalog()->getRules()._currency, getStartingMoney() );
        TileMapXMLData tileMap;
        if ( _tileMapPath.empty() || tileMap.load( AssetID{ _tileMapPath } ) == false )
        {
            SW_LOG_ERROR_ID( "NileGameMode.MapMissing", "[Nile] the game mode needs a readable _tileMapPath (got '%#') - the city cannot start", _tileMapPath.c_str() );
            return false;
        }
        vector<hashed_string> listTerrain;
        for ( const CatalogKey<CityTerrainDef>& terrain : _listTerrain )
        {
            listTerrain.push_back( terrain.key() );
        }
        CitySimulation& city = getMutableCity();
        if ( city.initialize( findCatalog(), tileMap._width, tileMap._height, _wallet ) == false )
            return false; // 범위 밖 도시 설정은 도시가 오류를 남겼다
        if ( NileCityPlanner::paintTerrain( city, tileMap, listTerrain ) == false )
            return false; // 모자란 땅 · 레이어는 플래너가 남겼다
        if ( _planner.reset( makeCityLayout(), AssetID{ _autoPlanPath.isSet() ? string_view{ _autoPlanPath.get() } : string_view{} } ) == false )
            return false; // 자동 계획을 읽지 못했다 — 이유는 플래너가 남겼다
        return beginCity();
    }

    WorldClockSettings NileGameModeComponent::makeClockSettings() const
    {
        WorldClockSettings settings;
        for ( const utf8* pMonth : { "Akhet1", "Akhet2", "Akhet3", "Akhet4", "Peret1", "Peret2", "Peret3", "Peret4", "Shemu1", "Shemu2", "Shemu3", "Shemu4" } )
        {
            settings._listSeason.push_back( hashed_string( pMonth ) );
        }
        settings._daysPerSeason = 1;
        settings._startHour     = 0.0f; // 첫 달도 꼭 한 달
        settings._secondsPerDay = _secondsPerMonth;
        return settings;
    }

    void NileGameModeComponent::settleMonths()
    {
        _listClockEvent.clear();
        _clock.pollEvents( _listClockEvent );
        for ( size_t index = 0; index < _listClockEvent.size(); ++index )
        {
            if ( _listClockEvent[index]._kind != WorldClockEvent::Kind::DayChanged )
                continue;
            // 같은 넘김의 알림(다음 날 넘김 전까지)에 해 넘김이 있으면 새 해다.
            bool bNewYear = false;
            for ( size_t next = index + 1; next < _listClockEvent.size() && _listClockEvent[next]._kind != WorldClockEvent::Kind::DayChanged; ++next )
            {
                bNewYear = bNewYear || _listClockEvent[next]._kind == WorldClockEvent::Kind::YearChanged;
            }
            getMutableCity().settleMonth( bNewYear );
        }
    }

    NileCityLayout NileGameModeComponent::makeCityLayout() const
    {
        NileCityLayout layout;
        layout._roadX             = _roadX;
        layout._farmX             = _farmX;
        layout._secondBlockPeople = _secondBlockPeople;
        layout._thirdBlockPeople  = _thirdBlockPeople;
        return layout;
    }

    // ------------------------------------------------------------------------------
    // 상태 쓰기 · 복원하기(핫 리로드 · 세이브)
    // ------------------------------------------------------------------------------
    void NileGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( NileGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        const bool bCity = io.valueOpened( getMutableCity(), [this]( CitySimulation& city )
        { return city.initialize( findCatalog(), getCity().getWidth(), getCity().getHeight(), _wallet ); } );
        const bool bRead = bCity && io.valueOpened( _planner, [this]( NileCityPlanner& planner )
        { return planner.reset( makeCityLayout(), AssetID{ _autoPlanPath.isSet() ? string_view{ _autoPlanPath.get() } : string_view{} } ); } ); // 자동 계획을 읽지 못하면 이유는 플래너가 남겼다
        if ( bRead == false )
            return;
        io.value( _wallet );
        if ( io.valueOpened( _clock, [this]( WorldClock& clock )
        { return clock.initialize( makeClockSettings() ); } ) == false )
            return;
        // 달 수 · 진화 수 · 자동 계획 토글은 상태 칸이다(`GameModeComponent::serializeState`). 배속은 `GameSpeedControl` 이 검사하며 되돌린다.
        float32 timeScale = getSpeed().getTimeScale();
        uint8   bPaused   = getSpeed().isPaused() ? SW_TRUE : SW_FALSE;
        io.value( timeScale );
        io.value( bPaused );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        if ( io.check( getMutableSpeed().restore( timeScale, bPaused != SW_FALSE ) ) ) // 범위 밖 배속(깨진 세이브)은 거절한다
            markLayoutChanged();
    }

    void NileGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO_ID( "NileGameMode.CityStateRestoredMonth", "[Nile] city state restored - month %#, population %#, money %#", _monthCount, getCity().getPopulation(),
                            _wallet.getBalance( getCity().getCurrency() ) );
        else
            SW_LOG_WARNING_ID( "NileGameMode.SavedCityStateDoes", "[Nile] the saved city state does not match this build - founding a new city" );
    }

    // ------------------------------------------------------------------------------
    // 알림 · 현황
    // ------------------------------------------------------------------------------
    void NileGameModeComponent::logCityEvents()
    {
        for ( const CityEvent& event : getStepEvents() )
        {
            switch ( event._kind )
            {
                case CityEvent::Kind::MonthEnded:
                {
                    ++_monthCount;
                    SW_LOG_INFO_ID( "NileGameMode.MonthPopMoneyNet", "[Nile] month %# pop %# money %# (net %#, workers %#/%#, houses up %#, avg level %#, culture %#%%)", _monthCount, getCity().getPopulation(),
                                    _wallet.getBalance( getCity().getCurrency() ), event._value, getCity().getEmployed(), getCity().getWorkforce(), _evolvedCount, getCity().computeAverageHouseLevel(),
                                    static_cast<int32>( getCity().computeCultureCoverage() * NileGameModeComponentInternal::kPercent ) );
                    _evolvedCount = 0;
                    break;
                }
                case CityEvent::Kind::HouseEvolved:
                {
                    ++_evolvedCount;
                    break;
                }
                case CityEvent::Kind::Flood:
                {
                    SW_LOG_INFO_ID( "NileGameMode.YearNileFloodedFarm", "[Nile] year %# - the Nile flooded, farm fertility %#%%", _clock.getYear(), event._value );
                    break;
                }
                case CityEvent::Kind::HouseDevolved:
                case CityEvent::Kind::GoodsDelivered:
                {
                    break;
                }
            }
        }
    }

    void NileGameModeComponent::logStatus() const
    {
        SW_LOG_INFO_ID( "NileGameMode.YearMonth", "[Nile] year %# month %# · plan %#/%#", _clock.getYear(), _clock.getSeasonIndex() + 1, _planner.getNextStep(), _planner.getStepCount() );
        CityBuilderGameModeComponent::logStatus();
    }

    bool NileGameModeComponent::isAutoPlanOn() const
    {
        return isAutoPlayOn() || _bAutoPlanToggle == SW_TRUE;
    }
} // namespace sw
