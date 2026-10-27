#include "pch.h"

#include "Core/Math/XorShift32.h"

#include "TestFramework/TestRegistry.h"

#include <algorithm>

namespace test
{
    namespace
    {
        struct TestSelectionInternal
        {
            /** @brief `--test_costs` 에 없는 케이스의 비용(초) — 대부분의 케이스는 수십 ms 다. */
            static constexpr float64 kDefaultCaseCostSeconds = 0.05;

            /**
             * @brief 피셔-예이츠 섞기입니다. 상태는 `XorShift32` 하나라 같은 씨앗이면 어느 플랫폼 · 표준 라이브러리에서나 같은 순서입니다.
             * @details `std::shuffle` 은 알고리즘을 표준 라이브러리마다 달리 구현해, 같은 씨앗을 준 재현 명령이 다른 플랫폼에서 다른 순서를 돌았습니다.
             */
            template <typename TList>
            static void shuffle( TList& inoutList, uint32& inoutState )
            {
                for ( size_t index = inoutList.size(); index > 1; --index )
                {
                    inoutState              = sw::XorShift32::next( inoutState );
                    const size_t otherIndex = static_cast<size_t>( inoutState % static_cast<uint32>( index ) );
                    std::swap( inoutList[index - 1], inoutList[otherIndex] );
                }
            }
        };
    } // namespace
} // namespace test

namespace test
{
    bool TestRegistry::isSelected( const TestCaseInfo& testInfo ) const
    {
        if ( _options._filter.matches( testInfo.fullName() ) == false )
            return false;
        if ( _catalog.isEnvironmentMissing( testInfo._groupName ) )
            return false;

        for ( size_t kindIndex = 0; kindIndex < static_cast<size_t>( SuiteKind::Count ); ++kindIndex )
        {
            const SuiteKind     kind       = static_cast<SuiteKind>( kindIndex );
            const SuiteKindMode mode       = _options.getKindMode( kind );
            const bool          bOfKind    = _catalog.isCaseOfKind( testInfo, kind );
            const bool          bExcluded  = mode == SuiteKindMode::Exclude && bOfKind;
            const bool          bNotChosen = mode == SuiteKindMode::Only && bOfKind == false;
            if ( bExcluded || bNotChosen )
                return false;
        }
        return true;
    }

    sw::vector<const TestCaseInfo*> TestRegistry::selectCasesForThisShard() const
    {
        const sw::vector<TestCaseInfo>& listCase = _catalog.getListCase();
        sw::vector<const TestCaseInfo*> listSelected;
        listSelected.reserve( listCase.size() );
        for ( const TestCaseInfo& testInfo : listCase )
        {
            if ( isSelected( testInfo ) )
                listSelected.push_back( &testInfo );
        }
        if ( _options._shardCount <= 1 )
            return listSelected;

        // 비싼 케이스부터 지금 가장 가벼운 샤드에 놓는다(LPT). 비용이 같으면 등록 순서라, 기본 비용 케이스는 샤드를 차례로 돌며 놓인다
        // — 한 스위트가 한 샤드에 몰리지 않는다. 모든 샤드가 같은 표 · 같은 비용으로 같은 배정을 계산한다.
        sw::vector<sw::pair<float64, size_t>> listCostAndIndex;
        listCostAndIndex.reserve( listSelected.size() );
        for ( size_t index = 0; index < listSelected.size(); ++index )
        {
            // 비용표(정책 파일) → 생성기가 준 예상 비용 → 기본값 순이다.
            const TestCaseInfo& testInfo      = *listSelected[index];
            const auto          costIt        = _options._mapCaseCost.find( testInfo.fullName() );
            const float64       generatedCost = testInfo._expectedSeconds > 0.0 ? testInfo._expectedSeconds : TestSelectionInternal::kDefaultCaseCostSeconds;
            listCostAndIndex.push_back( { costIt != _options._mapCaseCost.end() ? costIt->second : generatedCost, index } );
        }
        std::stable_sort( listCostAndIndex.begin(), listCostAndIndex.end(), []( const sw::pair<float64, size_t>& lhs, const sw::pair<float64, size_t>& rhs )
        {
            return lhs.first > rhs.first;
        } );

        sw::vector<float64> listShardLoad( _options._shardCount, 0.0 );
        sw::vector<uint8>   listMine( listSelected.size(), 0 );
        for ( const sw::pair<float64, size_t>& costAndIndex : listCostAndIndex )
        {
            const size_t lightestShard = static_cast<size_t>( std::min_element( listShardLoad.begin(), listShardLoad.end() ) - listShardLoad.begin() );
            listShardLoad[lightestShard] += costAndIndex.first;
            if ( lightestShard == _options._shardIndex )
                listMine[costAndIndex.second] = 1;
        }

        sw::vector<const TestCaseInfo*> listThisShard;
        for ( size_t index = 0; index < listSelected.size(); ++index )
        {
            if ( listMine[index] != 0 )
                listThisShard.push_back( listSelected[index] );
        }
        return listThisShard;
    }

    sw::vector<const TestCaseInfo*> TestRegistry::makeRunOrder( uint32 iteration ) const
    {
        sw::vector<const TestCaseInfo*> listRun = selectCasesForThisShard();
        if ( _options._bShuffle == false )
            return listRun;

        // 스위트 순서를 섞고 스위트 안의 케이스를 섞는다(스위트는 붙어 있다 — gtest 와 같다).
        sw::vector<sw::string>                               listSuiteOrder;
        sw::map<sw::string, sw::vector<const TestCaseInfo*>> mapSuiteCase;
        for ( const TestCaseInfo* pTestInfo : listRun )
        {
            sw::vector<const TestCaseInfo*>& listSuiteCase = mapSuiteCase[pTestInfo->_groupName];
            if ( listSuiteCase.empty() )
                listSuiteOrder.push_back( pTestInfo->_groupName );
            listSuiteCase.push_back( pTestInfo );
        }

        // 상태 0 은 xorshift 의 고정점이라 1 로 바꾼다(`--test_seed=0` 이나 `_randomSeed + iteration` 이 넘쳐 0 이 되는 경우).
        uint32 randomState = _options._randomSeed + iteration;
        if ( randomState == 0 )
            randomState = 1;
        TestSelectionInternal::shuffle( listSuiteOrder, randomState );
        listRun.clear();
        for ( const sw::string& suiteName : listSuiteOrder )
        {
            sw::vector<const TestCaseInfo*>& listSuiteCase = mapSuiteCase[suiteName];
            TestSelectionInternal::shuffle( listSuiteCase, randomState );
            listRun.insert( listRun.end(), listSuiteCase.begin(), listSuiteCase.end() );
        }
        return listRun;
    }

    sw::vector<sw::string> TestRegistry::collectEmptySuites( const sw::vector<CaseRecord>& listRecord ) const
    {
        // 스위트를 골라 놓고 **하나도 실행되지 않았다면** 그 스위트는 아무것도 검증하지 않았다.
        //
        // 스킵은 실패가 아니라서 그냥 두면 이런 실행이 초록이다. DXC 가 사라지거나 쿠킹된 셰이더가 없어지면 케이스가 스스로 SW_TEST_SKIP 하고,
        // CI 는 "통과" 를 보고한다 — 무엇이 사라졌는지 아무도 모른 채로. 검증 공백은 통과가 아니므로 실패로 만든다.
        // 구성 기능이 없어 건너뛴 케이스(`SW_TEST_REQUIRES_FEATURE`)는 세지 않는다 — "이 구성에 없다" 는 선언된 차이다. 그것만 남은 스위트는 판정하지 않는다.
        //
        // 샤드로 나눴으면 **여러 샤드에 갈린 스위트는 판단하지 않는다** — 이 샤드가 받은 케이스가 마침 모두 건너뛰는 것이어도 다른
        // 샤드에서는 검증했을 수 있다. 통째로 이 샤드에 온 스위트만 본다. 갈린 스위트가 "전부 건너뜀" 을 막아야 하면 그 전제를 단언하는
        // 케이스를 따로 둔다(`ReflectionParserTest.ParserExecutableIsBuilt`).
        struct SuiteTally
        {
            uint32 _ranCount{ 0 };
            uint32 _environmentSkipCount{ 0 };
            uint32 _caseCount{ 0 };
        };
        sw::vector<sw::string>          listSuiteOrder;
        sw::map<sw::string, SuiteTally> mapTally;
        for ( const CaseRecord& record : listRecord )
        {
            const sw::string& suiteName = record._pTestInfo->_groupName;
            if ( mapTally.find( suiteName ) == mapTally.end() )
                listSuiteOrder.push_back( suiteName );
            SuiteTally& tally = mapTally[suiteName];
            ++tally._caseCount;
            if ( record._result != CaseResult::Skipped )
                ++tally._ranCount;
            else if ( record._missingFeature == BuildFeature::Count )
                ++tally._environmentSkipCount;
        }

        sw::map<sw::string, uint32> mapSuiteSelectedCount;
        if ( _options._shardCount > 1 )
        {
            for ( const TestCaseInfo& testInfo : _catalog.getListCase() )
            {
                if ( isSelected( testInfo ) )
                    ++mapSuiteSelectedCount[testInfo._groupName];
            }
        }

        sw::vector<sw::string> listEmptySuite;
        for ( const sw::string& suiteName : listSuiteOrder )
        {
            const SuiteTally& tally       = mapTally[suiteName];
            const uint32      casesHere   = tally._caseCount / _options._repeatCount;
            const bool        bSplit      = _options._shardCount > 1 && mapSuiteSelectedCount[suiteName] > casesHere;
            const bool        bEmptySuite = tally._ranCount == 0 && tally._environmentSkipCount > 0 && bSplit == false;
            if ( bEmptySuite )
                listEmptySuite.push_back( suiteName + " (" + sw::to_string( tally._environmentSkipCount ) + " skipped)" );
        }
        return listEmptySuite;
    }
} // namespace test
