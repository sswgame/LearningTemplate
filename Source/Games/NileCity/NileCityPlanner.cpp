#include "pch.h"

#include "Games/NileCity/NileCityPlanner.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

namespace sw
{
    namespace
    {
        struct NileCityPlannerInternal
        {
            /** @brief 동쪽 사막이 시작하는 x(실수)입니다 — 모래 언덕처럼 베이크한다. */
            static float32 computeDesertEdge( const NileCityLayout& layout, int32 y )
            {
                return static_cast<float32>( layout._desertStartX ) + layout._desertDuneHeight * MathUtil::sin( static_cast<float32>( y ) * layout._desertDuneRate );
            }

            /**
             * @brief 순서표의 칸 글 "x,y" 를 읽습니다. x 는 수이거나 배치의 칸 이름(`road` = 남북 도로, `farm` = 농장 왼쪽)입니다.
             * @return 칸. 모양이 틀리면 그 칸 글이 대상인 오류입니다(부르는 쪽이 원소와 함께 알린다).
             */
            [[nodiscard]] static expected<int2> parseCell( const NileCityLayout& layout, string_view text )
            {
                const size_t comma = text.find( ',' );
                if ( comma == string_view::npos )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the cell has no comma (expected x,y)", text } );
                const string_view xText = StringUtil::trim( text.substr( 0, comma ) );
                const string_view yText = StringUtil::trim( text.substr( comma + 1 ) );
                int32             x{ 0 };
                if ( xText == "road" )
                    x = layout._roadX;
                else if ( xText == "farm" )
                    x = layout._farmX;
                else if ( StringUtil::parseInt( xText, x ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the cell's x is neither a number nor road / farm", text } );
                int32 y{ 0 };
                if ( StringUtil::parseInt( yText, y ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the cell's y is not a number", text } );
                return int2{ x, y };
            }

            /** @brief 구역 번호(1 · 2 · 3)의 시작 인구입니다. 1 구역은 처음부터, 2 · 3 구역은 배치의 인구 임계값이다. 모르는 번호면 그 번호가 대상인 오류입니다. */
            [[nodiscard]] static expected<int32> findBlockPopulation( const NileCityLayout& layout, int32 block )
            {
                const int32 arrBlockPopulation[] = { 0, layout._secondBlockPeople, layout._thirdBlockPeople };
                if ( block < 1 || block > 3 )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the block is not 1..3", to_string( block ) } );
                return arrBlockPopulation[block - 1];
            }

            /** @brief 사막의 바위 — 흩어진 돌(결정적 무늬)과 동쪽 끝 능선입니다. */
            static bool isRock( const NileCityLayout& layout, int32 x, int32 y )
            {
                if ( ( x * 7 + y * 3 ) % 13 == 0 )
                    return true;
                return x >= layout._rockRidgeX && ( y / 4 ) % 3 == 0;
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
    }

    void NileCityPlanner::serializeState( StateIO& io )
    {
        io.value( _nextStep );
        io.value( _skippedCount );
        io.check( 0 <= _nextStep && _nextStep <= static_cast<int32>( _listStep.size() ) && 0 <= _skippedCount );
    }

    float32 NileCityPlanner::computeRiverCenter( const NileCityLayout& layout, int32 y )
    {
        return layout._riverCenterX + layout._riverMeander * MathUtil::sin( static_cast<float32>( y ) * layout._riverMeanderRate );
    }

    void NileCityPlanner::paintTerrain( CitySimulation& city, const NileCityLayout& layout )
    {
        for ( int32 y = 0; y < city.getHeight(); ++y )
        {
            const float32 river  = computeRiverCenter( layout, y );
            const float32 desert = NileCityPlannerInternal::computeDesertEdge( layout, y );
            for ( int32 x = 0; x < city.getWidth(); ++x )
            {
                const float32 offset  = static_cast<float32>( x ) + 0.5f - river;
                CityTerrain   terrain = CityTerrain::Grass;
                if ( MathUtil::abs( offset ) <= layout._riverHalfWidth )
                    terrain = CityTerrain::Water;
                else if ( offset > 0.0f && offset <= layout._riverHalfWidth + layout._eastFloodWidth )
                    terrain = CityTerrain::Floodplain;
                else if ( offset < 0.0f && offset >= -( layout._riverHalfWidth + layout._westFloodWidth ) )
                    terrain = CityTerrain::Floodplain;
                else if ( offset < 0.0f || static_cast<float32>( x ) + 0.5f >= desert )
                    terrain = NileCityPlannerInternal::isRock( layout, x, y ) && offset > 0.0f ? CityTerrain::Rock : CityTerrain::Sand;
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

    void NileCityPlanner::addBuilding( const utf8* pBuildingID, int32 x, int32 y, int32 minPopulation )
    {
        PlanStep step;
        step._buildingID    = hashed_string( pBuildingID );
        step._from          = int2{ x, y };
        step._to            = step._from;
        step._minPopulation = minPopulation;
        _listStep.push_back( step );
    }

    bool NileCityPlanner::readPlanFile( const AssetID& assetID, vector<NileAutoPlanStep>& outListStep )
    {
        const string_view planPath = assetID.view();
        outListStep.clear();
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::loadRoot( document, assetID, "NileAutoPlan", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "NileCityPlanner.AutoPlanCouldNot", "[Nile] auto plan '%#' could not be read: %#", planPath, document.getLastError().c_str() );
            return false;
        }
        // 한 줄이 한 단계다(적힌 순서). 칸의 이름 · 필수 · 형식은 스키마(NileAutoPlanStepDef)가 정하고 readProperties 가 모르는 속성까지 막는다.
        bool bValid = true;
        for ( XMLNode node = root.findChild(); node.isValid(); node = node.findNextSibling() )
        {
            const string_view kind = node.getName();
            if ( kind.empty() )
                continue; // 주석 · 글 노드
            NileAutoPlanStep step{};
            step._bRoad                = kind == "Road";
            const bool bKnownKind      = step._bRoad || kind == "Building";
            const bool bPropertiesRead = bKnownKind && XMLFields::readProperties( node, step._def, planPath );
            const bool bBlockInPlan    = step._def._block >= 1 && step._def._block <= 3;
            // 도로는 건물 id 가 없고 건물은 있어야 한다.
            const bool bShapeValid = bPropertiesRead && bBlockInPlan && ( step._bRoad == step._def._id.empty() );
            if ( bShapeValid == false )
            {
                SW_LOG_ERROR_ID( "NileCityPlanner.AutoPlanNeedsX", "[Nile] auto plan '%#': <%#> needs from=\"x,y\" (to), block 1..3 (extra) and Building an id (Road none) - %#", planPath,
                                 node.getName(), node.toString().c_str() );
                bValid = false;
                continue;
            }
            outListStep.push_back( step );
        }
        if ( bValid == false )
            outListStep.clear();
        return bValid;
    }

    bool NileCityPlanner::reset( const NileCityLayout& layout, const AssetID& assetID )
    {
        const string_view planPath = assetID.view();
        using Internal             = NileCityPlannerInternal;
        _listStep.clear();
        _nextStep     = 0;
        _skippedCount = 0;

        vector<NileAutoPlanStep> listPlanStep;
        if ( readPlanFile( assetID, listPlanStep ) == false )
            return false;
        // Road 는 from → to 의 곧은 길, Building 은 from(과 to 가 있으면 그 사이 모든 칸, 행 우선)의 건물이다.
        // 인구 임계값은 구역(block)의 시작 인구 + extra 다. 칸의 x 는 수이거나 배치의 이름(road · farm)이다.
        for ( const NileAutoPlanStep& planStep : listPlanStep )
        {
            const NileAutoPlanStepDef& def        = planStep._def;
            const expected<int2>       from       = Internal::parseCell( layout, def._from );
            const expected<int2>       to         = def._to.empty() ? from : Internal::parseCell( layout, def._to );
            const expected<int32>      population = Internal::findBlockPopulation( layout, def._block );
            const Error*               pError     = from.has_value() == false ? &from.error() : to.has_value() == false     ? &to.error()
                                                                                          : population.has_value() == false ? &population.error()
                                                                                                                            : nullptr;
            if ( pError != nullptr )
            {
                SW_LOG_ERROR_ID( "NileCityPlanner.AutoPlanStepBlock", "[Nile] auto plan '%#': step from '%#' to '%#' (block %#) does not resolve on this layout: %# (%#)", planPath, def._from, def._to,
                                 def._block, pError->message(), pError->detail() );
                _listStep.clear();
                return false;
            }
            const int2  fromCell      = *from;
            const int2  toCell        = *to;
            const int32 minPopulation = *population + def._extra;
            if ( planStep._bRoad )
            {
                addRoad( fromCell, toCell, minPopulation );
                continue;
            }
            for ( int32 y = MathUtil::min( fromCell._y, toCell._y ); y <= MathUtil::max( fromCell._y, toCell._y ); ++y )
            {
                for ( int32 x = MathUtil::min( fromCell._x, toCell._x ); x <= MathUtil::max( fromCell._x, toCell._x ); ++x )
                {
                    addBuilding( def._id.c_str(), x, y, minPopulation );
                }
            }
        }
        return true;
    }

    int32 NileCityPlanner::advance( CitySimulation& city, const Wallet& wallet, int32 roadCost )
    {
        int32 doneCount = 0;
        while ( isFinished() == false )
        {
            const PlanStep& step = _listStep[static_cast<size_t>( _nextStep )];
            if ( city.getPopulation() < step._minPopulation )
                break;
            if ( step._buildingID.empty() )
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
            const CityPlaceResult result = city.placeBuilding( step._buildingID, step._from._x, step._from._y );
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
