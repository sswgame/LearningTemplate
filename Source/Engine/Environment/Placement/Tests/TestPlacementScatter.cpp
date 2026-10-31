#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Placement/PlacementRule.h"

using namespace sw;

namespace
{
    /** @brief 후보 위치(레인 0 · 1 해시)를 다시 구해 최소 거리 다트 던지기를 O(n²) 로 그대로 돌린 기준 결과입니다. */
    vector<float2> resolveByBruteForce( const PlacementRule& rule, const PlacementRegion& region, const vector<PlacementCandidate>& listCandidate )
    {
        const float2   size{ region._max._x - region._min._x, region._max._y - region._min._y };
        vector<float2> listAccepted;
        for ( uint32 candidateIndex = 0; candidateIndex < listCandidate.size(); ++candidateIndex )
        {
            if ( listCandidate[candidateIndex]._bPassed == SW_FALSE )
                continue;
            const float2 position{ region._min._x + PlacementScatter::toUnit( PlacementScatter::hashCandidate( rule._seed, candidateIndex, 0 ) ) * size._x,
                                   region._min._y + PlacementScatter::toUnit( PlacementScatter::hashCandidate( rule._seed, candidateIndex, 1 ) ) * size._y };
            bool         bTooClose = false;
            for ( const float2& other : listAccepted )
            {
                const float32 deltaU = other._x - position._x;
                const float32 deltaV = other._y - position._y;
                if ( deltaU * deltaU + deltaV * deltaV < rule._minDistance * rule._minDistance )
                {
                    bTooClose = true;
                    break;
                }
            }
            if ( bTooClose == false )
                listAccepted.push_back( position );
        }
        return listAccepted;
    }

    void expectSameAsBruteForce( float32 regionSide, float32 density, float32 minDistance )
    {
        PlacementRule rule;
        rule._density     = density;
        rule._minDistance = minDistance;
        rule._seed        = 7u;
        const PlacementRegion region{
            float2{-0.5f * regionSide, -0.5f * regionSide},
            float2{ 0.5f * regionSide,  0.5f * regionSide}
        };
        vector<PlacementCandidate> listCandidate;
        PlacementScatter::sampleCandidates( rule, region, nullptr, {}, listCandidate );
        vector<PlacementInstance> listInstance;
        PlacementScatter::resolveCandidates( rule, region, listCandidate, {}, listInstance );

        const vector<float2> listExpected = resolveByBruteForce( rule, region, listCandidate );
        SW_REQUIRE_EQUAL( listExpected.size(), listInstance.size() );
        for ( size_t index = 0; index < listExpected.size(); ++index )
        {
            SW_EXPECT_TRUE( listExpected[index]._x == listInstance[index]._planePosition._x && listExpected[index]._y == listInstance[index]._planePosition._y );
        }
    }
} // namespace

// 최소 거리 격자(조밀 배열)는 탐욕 다트 던지기와 같은 점을 같은 순서로 받아들인다 - 셀 하나에 점 하나인 보통 크기.
SW_TEST_CASE( PlacementScatterTest, MinDistanceMatchesBruteForce )
{
    expectSameAsBruteForce( 40.0f, 2.0f, 0.8f );
}

// 영역이 커서 셀 수 상한에 걸리면 셀을 키운다(한 셀에 점이 여럿) - 그래도 결과는 같다.
SW_TEST_CASE( PlacementScatterTest, MinDistanceMatchesBruteForceWithCappedGrid )
{
    expectSameAsBruteForce( 4000.0f, 0.0002f, 0.5f );
}
