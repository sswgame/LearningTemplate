#include "pch.h"

#include "Games/NileCity/NileCityPlanner.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Kits/Strategy/CityBuilder/CitySimulation.h"

namespace sw
{
    namespace
    {
        struct NileCityPlannerInternal
        {
            static constexpr float32 kRiverHalfWidth    = 1.5f; ///< 강 반폭(칸)
            static constexpr float32 kEastFloodWidth    = 8.0f; ///< 동쪽 범람원 폭 — 농장 · 도시가 이쪽에 선다
            static constexpr float32 kWestFloodWidth    = 3.0f;
            static constexpr int32   kRoadX             = 17; ///< 범람원 농장과 도시 사이 남북 도로
            static constexpr int32   kFarmX             = 14; ///< 3×3 농장의 왼쪽 — x 13..16 은 어느 줄에서나 범람원이다
            static constexpr int32   kDesertStartX      = 40;
            static constexpr int32   kSecondBlockPeople = 60;
            static constexpr int32   kThirdBlockPeople  = 160;

            /** @brief 동쪽 사막이 시작하는 x(실수)입니다 — 모래 언덕처럼 굽는다. */
            static float32 computeDesertEdge( int32 y )
            {
                return static_cast<float32>( kDesertStartX ) + 1.5f * MathUtil::sin( static_cast<float32>( y ) * 0.3f );
            }

            /** @brief 사막의 바위 — 흩어진 돌과 동쪽 끝 능선입니다. */
            static bool isRock( int32 x, int32 y )
            {
                if ( ( x * 7 + y * 3 ) % 13 == 0 )
                    return true;
                return x >= 45 && ( y / 4 ) % 3 == 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NileCityPlanner::NileCityPlanner()
        : _listStep{}
        , _nextStep{ 0 }
        , _skippedCount{ 0 }
    {
        reset();
    }

    void NileCityPlanner::writeState( Archive& outArchive ) const
    {
        outArchive << _nextStep;
        outArchive << _skippedCount;
    }

    bool NileCityPlanner::readState( Archive& archive )
    {
        int32 nextStep     = 0;
        int32 skippedCount = 0;
        archive >> nextStep;
        archive >> skippedCount;
        const bool bStepValid = 0 <= nextStep && nextStep <= static_cast<int32>( _listStep.size() );
        if ( archive.isError() || bStepValid == false || skippedCount < 0 )
            return false;
        _nextStep     = nextStep;
        _skippedCount = skippedCount;
        return true;
    }

    float32 NileCityPlanner::computeRiverCenter( int32 y )
    {
        return 9.0f + 2.0f * MathUtil::sin( static_cast<float32>( y ) * 0.22f );
    }

    void NileCityPlanner::paintTerrain( CitySimulation& city )
    {
        for ( int32 y = 0; y < city.getHeight(); ++y )
        {
            const float32 river  = computeRiverCenter( y );
            const float32 desert = NileCityPlannerInternal::computeDesertEdge( y );
            for ( int32 x = 0; x < city.getWidth(); ++x )
            {
                const float32 offset  = static_cast<float32>( x ) + 0.5f - river;
                CityTerrain   terrain = CityTerrain::Grass;
                if ( MathUtil::abs( offset ) <= NileCityPlannerInternal::kRiverHalfWidth )
                    terrain = CityTerrain::Water;
                else if ( offset > 0.0f && offset <= NileCityPlannerInternal::kRiverHalfWidth + NileCityPlannerInternal::kEastFloodWidth )
                    terrain = CityTerrain::Floodplain;
                else if ( offset < 0.0f && offset >= -( NileCityPlannerInternal::kRiverHalfWidth + NileCityPlannerInternal::kWestFloodWidth ) )
                    terrain = CityTerrain::Floodplain;
                else if ( offset < 0.0f || static_cast<float32>( x ) + 0.5f >= desert )
                    terrain = NileCityPlannerInternal::isRock( x, y ) && offset > 0.0f ? CityTerrain::Rock : CityTerrain::Sand;
                city.setTerrain( x, y, terrain );
            }
        }
    }

    void NileCityPlanner::addRoad( const int2& from, const int2& to, int32 minPopulation )
    {
        PlanStep step;
        step._from          = from;
        step._to            = to;
        step._minPopulation = minPopulation;
        _listStep.push_back( step );
    }

    void NileCityPlanner::addBuilding( const utf8* pBuildingId, int32 x, int32 y, int32 minPopulation )
    {
        PlanStep step;
        step._buildingId    = hashed_string( pBuildingId );
        step._from          = int2{ x, y };
        step._to            = step._from;
        step._minPopulation = minPopulation;
        _listStep.push_back( step );
    }

    void NileCityPlanner::reset()
    {
        using Internal = NileCityPlannerInternal;
        _listStep.clear();
        _nextStep              = 0;
        _skippedCount          = 0;
        const auto addHouseRow = [this]( int32 minX, int32 maxX, int32 y, int32 minPopulation )
        {
            for ( int32 x = minX; x <= maxX; ++x )
                addBuilding( "house", x, y, minPopulation );
        };

        // 1 구역(x 18..26, y 5..13) — 도로 고리 · 우물 · 농장 · 창고 · 바자 · 집 · 신전.
        addRoad( int2{ Internal::kRoadX, 4 }, int2{ 27, 4 }, 0 );
        addRoad( int2{ Internal::kRoadX, 4 }, int2{ Internal::kRoadX, 14 }, 0 );
        addRoad( int2{ 27, 4 }, int2{ 27, 14 }, 0 );
        addRoad( int2{ Internal::kRoadX, 14 }, int2{ 27, 14 }, 0 );
        addBuilding( "well", 19, 11, 0 );
        addBuilding( "well", 23, 11, 0 );
        addBuilding( "grain_farm", Internal::kFarmX, 5, 0 );
        addBuilding( "grain_farm", Internal::kFarmX, 8, 0 );
        addBuilding( "granary", 18, 5, 0 );
        addBuilding( "bazaar", 21, 5, 0 );
        addHouseRow( 18, 26, 13, 0 );
        addHouseRow( 18, 26, 12, 0 );
        addBuilding( "shrine", 23, 5, 0 );
        for ( int32 y = 7; y <= 10; ++y )
            addBuilding( "house", 26, y, 0 );
        addBuilding( "garden", 21, 11, 0 );
        addBuilding( "garden", 25, 11, 0 );
        addBuilding( "tax_collector", 18, 8, 20 );
        addBuilding( "juggler", 24, 5, 40 );
        addBuilding( "apothecary", 18, 10, 60 );
        addBuilding( "fig_farm", Internal::kFarmX, 11, 60 );

        // 2 구역(x 28..36, y 5..13) — 물 공급 · 공방 · 저장 마당 · 두 번째 바자 · 학교.
        const int32 second = Internal::kSecondBlockPeople;
        addRoad( int2{ 27, 4 }, int2{ 37, 4 }, second );
        addRoad( int2{ 37, 4 }, int2{ 37, 14 }, second );
        addRoad( int2{ 27, 14 }, int2{ 37, 14 }, second );
        addBuilding( "well", 30, 11, second );
        addBuilding( "well", 34, 11, second );
        addHouseRow( 28, 36, 13, second );
        addHouseRow( 28, 36, 12, second );
        addBuilding( "water_supply", 28, 9, second );
        addBuilding( "shrine", 36, 11, second );
        addBuilding( "apothecary", 28, 11, second );
        addBuilding( "statue", 32, 11, second );
        addBuilding( "potter", 28, 5, second );
        addBuilding( "storage_yard", 34, 5, second );
        addBuilding( "bazaar", 32, 5, second + 40 );
        addBuilding( "brewery", 30, 5, second + 60 );
        addBuilding( "scribe_school", 35, 9, second + 60 );
        addBuilding( "garden", 31, 9, second + 60 );
        addBuilding( "garden", 33, 9, second + 60 );

        // 3 구역(y 15..23) — 농장을 늘리고 집 네 줄 · 광장 · 둘째 서비스.
        const int32 third = Internal::kThirdBlockPeople;
        addRoad( int2{ Internal::kRoadX, 14 }, int2{ Internal::kRoadX, 24 }, third );
        addRoad( int2{ 27, 14 }, int2{ 27, 24 }, third );
        addRoad( int2{ 37, 14 }, int2{ 37, 24 }, third );
        addRoad( int2{ Internal::kRoadX, 24 }, int2{ 37, 24 }, third );
        addBuilding( "grain_farm", Internal::kFarmX, 15, third );
        addBuilding( "fig_farm", Internal::kFarmX, 18, third );
        addBuilding( "grain_farm", Internal::kFarmX, 21, third );
        addBuilding( "well", 20, 18, third );
        addBuilding( "well", 30, 18, third );
        addBuilding( "well", 34, 18, third );
        addHouseRow( 18, 26, 15, third );
        addHouseRow( 28, 36, 15, third );
        addHouseRow( 18, 26, 16, third );
        addHouseRow( 28, 36, 16, third );
        addBuilding( "shrine", 18, 19, third );
        addBuilding( "apothecary", 28, 19, third );
        addBuilding( "juggler", 35, 19, third );
        addBuilding( "granary", 24, 19, third );
        addBuilding( "plaza", 21, 19, third );
        addBuilding( "plaza", 31, 19, third );
        addBuilding( "well", 20, 21, third + 40 );
        addBuilding( "well", 30, 21, third + 40 );
        addBuilding( "well", 34, 21, third + 40 );
        addHouseRow( 18, 26, 23, third + 40 );
        addHouseRow( 28, 36, 23, third + 40 );
        addHouseRow( 18, 26, 22, third + 40 );
        addHouseRow( 28, 36, 22, third + 40 );
        addBuilding( "statue", 23, 21, third + 80 );
        addBuilding( "statue", 33, 21, third + 80 );
        addBuilding( "tax_collector", 18, 17, third + 80 );
    }

    int32 NileCityPlanner::advance( CitySimulation& city, const Wallet& wallet, int32 roadCost )
    {
        int32 doneCount = 0;
        while ( isFinished() == false )
        {
            const PlanStep& step = _listStep[static_cast<size_t>( _nextStep )];
            if ( city.getPopulation() < step._minPopulation )
                break;
            if ( step._buildingId.empty() )
            {
                // 도로 — 다 깔 돈이 될 때까지 기다린다(반쯤 깐 길은 끊긴 길이다).
                const int32 tileCount = MathUtil::abs( step._to._x - step._from._x ) + MathUtil::abs( step._to._y - step._from._y ) + 1;
                if ( wallet.canAfford( city.getCurrency(), static_cast<int64>( tileCount ) * roadCost ) == false )
                    break;
                (void)city.placeRoadLine( step._from, step._to );
                ++_nextStep;
                ++doneCount;
                continue;
            }
            const CityPlaceResult result = city.placeBuilding( step._buildingId, step._from._x, step._from._y );
            if ( result == CityPlaceResult::NotEnoughMoney )
                break;
            if ( result != CityPlaceResult::Ok )
                ++_skippedCount;
            else
                ++doneCount;
            ++_nextStep;
        }
        return doneCount;
    }
} // namespace sw
