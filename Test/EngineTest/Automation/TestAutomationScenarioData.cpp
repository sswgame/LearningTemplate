#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Automation/AutomationStepValidation.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// 시나리오 데이터 — 앱을 띄우지 않고 모든 `*.scenario.xml` 의 단계 속성(이름 · 값 · 필수 속성)을 등록된 검사 함수로 본다.
SW_LOG_CALLER( "TestAutomationScenarioData" );

namespace
{
    struct AutomationScenarioDataInternal
    {
        /** @brief 시나리오 폴더 전체의 검사 합계입니다. 못 본 단계는 통과와 섞지 않고 따로 센다. */
        struct Tally
        {
            uint32                      _scenarioCount{ 0 };
            uint32                      _fullySeenScenarioCount{ 0 };  ///< 모든 단계의 검사 함수를 불러 본 시나리오
            uint32                      _validatedStepCount{ 0 };      ///< 검사 함수를 불러 통과한 단계
            uint32                      _noValidatorStepCount{ 0 };    ///< 등록된 검사 함수가 없는 단계(볼 속성 검사가 없다 - 통과로 세지 않는다)
            uint32                      _unseenStepCount{ 0 };         ///< 이 프로세스에 모듈이 올라오지 않아 검사 함수를 부르지 못한 단계
            sw::map<sw::string, uint32> _mapUnseenReasonToStepCount{}; ///< "<까닭>: <이름>" → 그 이유로 못 본 단계 수
        };

        /** @brief 데이터 폴더에서 시나리오 파일을 가려냅니다. */
        static bool isScenarioFile( sw::string_view resourceID ) { return sw::StringUtil::endsWith( resourceID, ".scenario.xml", true ); }

        /**
         * @brief 시나리오 한 편의 단계를 검사합니다. 틀린 단계마다 테스트 실패를 남깁니다.
         * @return 모든 단계의 검사 함수를 불러 봤으면 true, 못 본 단계가 하나라도 있으면 false 입니다.
         */
        static bool validateScenario( const sw::string& resourceID, Tally& inoutTally )
        {
            sw::AutomationScenario scenario;
            sw::string             loadError;
            const bool             bLoaded = scenario.loadFromPath( resourceID, loadError );
            if ( bLoaded == false )
            {
                SW_EXPECT_TRUE_MSG( bLoaded, ( "시나리오를 읽지 못해 단계를 하나도 보지 못했습니다: " + resourceID + " - " + loadError ).c_str() );
                return false;
            }
            bool bFullySeen = true;
            for ( const sw::AutomationStep& step : scenario.getSteps() )
            {
                // 같은 프레임에 혼자 있어야 하는 단계는 실행기의 준비 함수가 보지만, 시나리오만 있으면 되는 검사라 여기서도 같은 함수로 본다.
                const bool bWaitUntil = step._kind == "WaitUntil";
                if ( bWaitUntil )
                {
                    const sw::AutomationStep* pOtherStep = scenario.findOtherStepAtSameFrame( step );
                    SW_EXPECT_TRUE_MSG( pOtherStep == nullptr,
                                        ( resourceID + ": " + step.describe() + " must be the only step at its frame (also " + ( pOtherStep != nullptr ? pOtherStep->describe() : sw::string() ) + ")" ).c_str() );
                }

                sw::string                         unresolvedName;
                const sw::AutomationStepDependency dependency = sw::AutomationStepValidation::findUnresolvedDependency( step, unresolvedName );
                if ( dependency != sw::AutomationStepDependency::Resolved )
                {
                    ++inoutTally._unseenStepCount;
                    ++inoutTally._mapUnseenReasonToStepCount[sw::string( sw::AutomationStepValidation::getDependencyLabel( dependency ) ) + ": " + unresolvedName];
                    bFullySeen = false;
                    continue;
                }
                const sw::AutomationStepRegistration* pRegistration = sw::AutomationStepRegistry::find( step._kind ); // 종류가 등록돼 있다는 것은 위에서 확인했다
                if ( pRegistration == nullptr || pRegistration->_pValidate == nullptr )
                {
                    ++inoutTally._noValidatorStepCount;
                    continue;
                }
                sw::string stepError;
                const bool bValid = pRegistration->_pValidate( step, stepError );
                if ( bValid )
                    ++inoutTally._validatedStepCount;
                else
                    SW_EXPECT_TRUE_MSG( bValid, ( resourceID + ": " + stepError ).c_str() );
            }
            return bFullySeen;
        }
    };
} // namespace

/**
 * @brief [AutomationScenarioDataTest] 모든 시나리오의 단계가 등록된 검사 함수를 통과한다 - 앱을 띄우기 전에 속성 이름 · 값 · 필수 속성 오류를 잡는다
 * @details 실행기는 시작 프레임에서야 단계를 검사해 GPU 검증까지 가야 오류가 드러났다. 검사 함수(`AutomationStepRegistration::_pValidate`)는 순수 함수라 이 프로세스에 등록된
 *          단계는 실행기 없이 부른다. 에디터 · 게임 모듈이 등록하는 단계 · 탐침 · 변수 · 개발 명령은 이 프로세스에 없어 부를 수 없고(`AutomationStepValidation`),
 *          그 단계는 통과가 아니라 "못 본 단계" 로 합계와 이유별로 로그에 적는다. 시나리오를 훑으므로 새 시나리오는 자동으로 들어온다.
 *          입력 맵 · 창이 필요한 준비 함수(`_pPrepare`)는 실행기가 있어야 해서 이 테스트가 보지 않는다.
 */
SW_TEST_CASE( AutomationScenarioDataTest, EveryScenarioStepPassesItsValidation )
{
    SW_TEST_REQUIRES_FEATURE( SourceResources );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string& resourceRoot = sw::ResourcePaths::getRootFolderPath();
    SW_ASSERT_FALSE( resourceRoot.empty() );

    sw::vector<sw::string> listFilePath;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( resourceRoot, "", listFilePath, true ) );

    AutomationScenarioDataInternal::Tally tally;
    for ( const sw::string& filePath : listFilePath )
    {
        const sw::string resourceID = sw::ResourcePaths::toResourceID( filePath );
        if ( AutomationScenarioDataInternal::isScenarioFile( resourceID ) == false )
            continue;
        ++tally._scenarioCount;
        if ( AutomationScenarioDataInternal::validateScenario( resourceID, tally ) )
            ++tally._fullySeenScenarioCount;
    }

    SW_LOG_INFO( "[ScenarioData] %# scenarios: %# fully validated, %# partly (steps: %# validated, %# without a validator, %# not seen - their module is not loaded here)", tally._scenarioCount,
                 tally._fullySeenScenarioCount, tally._scenarioCount - tally._fullySeenScenarioCount, tally._validatedStepCount, tally._noValidatorStepCount, tally._unseenStepCount );
    for ( const auto& unseen : tally._mapUnseenReasonToStepCount )
    {
        SW_LOG_INFO( "[ScenarioData] not seen, %# step(s) - %#", unseen.second, unseen.first );
    }

    // 폴더를 못 찾아 0 편을 훑거나, 모든 단계가 못 본 단계로 빠지면 이 테스트는 아무것도 보지 않은 것이다.
    SW_EXPECT_TRUE_MSG( tally._scenarioCount >= 10u, ( "훑은 시나리오가 " + std::to_string( tally._scenarioCount ) + " 편뿐입니다" ).c_str() );
    SW_EXPECT_TRUE_MSG( tally._validatedStepCount > 0u, "검사 함수를 부른 단계가 하나도 없습니다 - 엔진 단계가 등록되지 않았습니다" );
}
