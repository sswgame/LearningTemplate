#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Automation/AutomationStepValidation.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// 시나리오 데이터 — 앱을 띄우지 않고 모든 `*.scenario.xml` 의 단계 속성(이름 · 값 · 필수 속성)을 등록된 검사 함수로 본다.
SW_LOG_CALLER( "TestAutomationScenarioData" );

namespace
{
    struct AutomationScenarioDataInternal
    {
        /** @brief 탐침 이름 하나를 받아 주는 대역입니다. 질의 탐침(`target` · `key` 를 받는다)이며 값은 내지 않는다. */
        [[nodiscard]] static bool readNothing( const sw::AutomationProbeQuery& /*query*/, sw::AutomationProbeValue& /*outValue*/ ) { return false; }

        struct StandInProbe
        {
            sw::string                      _name;
            sw::AutomationProbeRegistration _registration;

            explicit StandInProbe( sw::string_view name )
                : _name{ name }
                , _registration{ _name.c_str(), "stand-in for a probe whose module is not loaded in this process", nullptr, &readNothing, false, false }
            {
            }
        };

#if SW_DEV_COMMANDS_ENABLED
        [[nodiscard]] static bool runNothing( const sw::vector<sw::string>& /*listArgument*/, sw::string& /*outReply*/ ) { return false; }

        struct StandInCommand
        {
            sw::string                 _name;
            sw::DevCommandRegistration _registration;

            explicit StandInCommand( sw::string_view name )
                : _name{ name }
                , _registration{ _name.c_str(), "", "stand-in for a dev command whose module is not loaded in this process", &runNothing }
            {
            }
        };
#endif

        /**
         * @brief 이 프로세스에 모듈이 올라오지 않은 탐침 · 개발 명령 · 변수(`Variable` 단계)의 이름을 받아 주는 대역을 올려 둡니다(테스트가 끝나면 모두 뺀다).
         * @details 탐침 · 개발 명령의 검사는 이름이 등록돼 있어야 나머지 속성 검사로 넘어간다. 대역은 이름만 받아 주므로, 그 단계는 속성 이름 · 값 · 필수 속성을 보지만
         *          **이름이 실제로 있는지와 탐침이 `target` · `key` 를 요구하는지는 보지 못한다** - 그 둘은 소스를 읽는 린트(`CheckNameReferences`)가 본다. 변수 대역은 글 타입이라 값 읽기는 보지 않는다(`Variable` 은 값을 읽지 않는다).
         */
        class StandIns
        {
        public:
            StandIns() = default;
            ~StandIns()
            {
                if ( _listVariableValue.empty() == false )
                    sw::engine::getSubsystem<sw::GlobalVariableManager>().unregisterVariablesByModule( kVariableModule );
                for ( const sw::unique_ptr<StandInProbe>& pProbe : _listProbe )
                {
                    sw::AutomationProbes::unregisterProbe( &pProbe->_registration );
                }
#if SW_DEV_COMMANDS_ENABLED
                for ( const sw::unique_ptr<StandInCommand>& pCommand : _listCommand )
                {
                    sw::DevCommandRegistry::get().unregisterCommand( &pCommand->_registration );
                }
#endif
            }
            StandIns( const StandIns& )            = delete;
            StandIns& operator=( const StandIns& ) = delete;

            /** @brief 대역을 올렸으면 true 입니다(이름이 비었거나 이미 있으면 올리지 못한다). */
            bool addProbe( sw::string_view name )
            {
                sw::unique_ptr<StandInProbe> pProbe = sw::make_unique<StandInProbe>( name );
                if ( sw::AutomationProbes::registerProbe( &pProbe->_registration ) == false )
                    return false;
                _listProbe.push_back( std::move( pProbe ) );
                return true;
            }

            /** @brief 글 변수 대역을 올렸으면 true 입니다. 값을 읽는 검사(`ExpectVariable`)는 변수 타입을 알아야 해서 `Variable` 단계에만 씁니다. */
            bool addVariable( sw::string_view name )
            {
                if ( sw::engine::areEngineServicesBound() == false )
                    return false;
                sw::unique_ptr<sw::string> pValue = sw::make_unique<sw::string>();
                const bool                 bAdded = sw::engine::getSubsystem<sw::GlobalVariableManager>().registerVariable(
                    name, sw::GlobalVariableType::String, pValue.get(), std::variant<bool, int32, float32, sw::string>{ sw::string() }, "stand-in for a variable whose module is not loaded in this process", "",
                    kVariableModule, static_cast<uint32>( sizeof( sw::string ) ), true );
                if ( bAdded == false )
                    return false;
                _listVariableValue.push_back( std::move( pValue ) );
                return true;
            }

            bool addCommand( [[maybe_unused]] sw::string_view name )
            {
#if SW_DEV_COMMANDS_ENABLED
                sw::unique_ptr<StandInCommand> pCommand = sw::make_unique<StandInCommand>( name );
                if ( sw::DevCommandRegistry::get().registerCommand( &pCommand->_registration ) == false )
                    return false;
                _listCommand.push_back( std::move( pCommand ) );
                return true;
#else
                return false;
#endif
            }

        private:
            /** @brief 대역 변수를 등록한 모듈 이름입니다(끝에 이 이름으로 한꺼번에 뺀다). */
            static constexpr const utf8* kVariableModule = "ScenarioDataStandIn";

            sw::vector<sw::unique_ptr<StandInProbe>> _listProbe{};
            sw::vector<sw::unique_ptr<sw::string>>   _listVariableValue{};
#if SW_DEV_COMMANDS_ENABLED
            sw::vector<sw::unique_ptr<StandInCommand>> _listCommand{};
#endif
        };

        /** @brief 시나리오 폴더 전체의 검사 합계입니다. 못 본 단계는 통과와 섞지 않고 따로 센다. */
        struct Tally
        {
            uint32                      _scenarioCount{ 0 };
            uint32                      _fullySeenScenarioCount{ 0 };  ///< 모든 단계의 검사 함수를 불러 본 시나리오
            uint32                      _stepCount{ 0 };               ///< 시나리오의 모든 단계
            uint32                      _validatedStepCount{ 0 };      ///< 검사 함수를 불러 통과한 단계
            uint32                      _validatorOnlyStepCount{ 0 };  ///< 위 가운데 단계 등록 없이 검사 함수만 올라온 단계(에디터 · App 모듈이 없는 프로세스)
            uint32                      _standInStepCount{ 0 };        ///< 위 가운데 탐침 · 개발 명령 이름을 대역으로 받아 준 단계(이름 · 탐침 요구는 린트가 본다)
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
        static bool validateScenario( const sw::ResourceID& id, StandIns& inoutStandIn, Tally& inoutTally )
        {
            const sw::string&      resourceID = id.str();
            sw::AutomationScenario scenario;
            sw::string             loadError;
            const bool             bLoaded = scenario.loadFromPath( id, loadError );
            if ( bLoaded == false )
            {
                SW_EXPECT_TRUE_MSG( bLoaded, ( "시나리오를 읽지 못해 단계를 하나도 보지 못했습니다: " + resourceID + " - " + loadError ).c_str() );
                return false;
            }
            bool bFullySeen = true;
            for ( const sw::AutomationStep& step : scenario.getSteps() )
            {
                ++inoutTally._stepCount;
                // 같은 프레임에 혼자 있어야 하는 단계는 실행기의 준비 함수가 보지만, 시나리오만 있으면 되는 검사라 여기서도 같은 함수로 본다.
                const bool bWaitUntil = step._kind == "WaitUntil";
                if ( bWaitUntil )
                {
                    const sw::AutomationStep* pOtherStep = scenario.findOtherStepAtSameFrame( step );
                    SW_EXPECT_TRUE_MSG( pOtherStep == nullptr,
                                        ( resourceID + ": " + step.describe() + " must be the only step at its frame (also " + ( pOtherStep != nullptr ? pOtherStep->describe() : sw::string() ) + ")" ).c_str() );
                }

                // 탐침 · 개발 명령 이름이 모르는 것이면 대역을 올리고 다시 본다(한 단계가 둘을 묻지는 않지만 변수 이름이 그 뒤에 올 수 있어 되풀이한다).
                sw::string                   unresolvedName;
                sw::AutomationStepDependency dependency    = sw::AutomationStepValidation::findUnresolvedDependency( step, unresolvedName );
                bool                         bStandIn      = false;
                const bool                   bVariableStep = step._kind == "Variable";
                for ( uint32 attempt = 0; attempt < 3; ++attempt )
                {
                    bool bAdded = false;
                    if ( dependency == sw::AutomationStepDependency::Probe )
                        bAdded = inoutStandIn.addProbe( unresolvedName );
                    else if ( dependency == sw::AutomationStepDependency::DevCommand )
                        bAdded = inoutStandIn.addCommand( unresolvedName );
                    else if ( dependency == sw::AutomationStepDependency::GlobalVariable && bVariableStep )
                        bAdded = inoutStandIn.addVariable( unresolvedName );
                    if ( bAdded == false )
                        break;
                    bStandIn   = true;
                    dependency = sw::AutomationStepValidation::findUnresolvedDependency( step, unresolvedName );
                }
                if ( dependency != sw::AutomationStepDependency::Resolved )
                {
                    ++inoutTally._unseenStepCount;
                    ++inoutTally._mapUnseenReasonToStepCount[sw::string( sw::AutomationStepValidation::getDependencyLabel( dependency ) ) + ": " + unresolvedName];
                    bFullySeen = false;
                    continue;
                }
                sw::AutomationStepValidateFunction pValidate = nullptr;
                if ( sw::AutomationStepValidation::findValidator( step._kind, pValidate ) != sw::AutomationValidatorLookup::Found )
                {
                    ++inoutTally._noValidatorStepCount;
                    continue;
                }
                sw::string stepError;
                const bool bValid = pValidate( step, stepError );
                if ( bValid == false )
                {
                    SW_EXPECT_TRUE_MSG( bValid, ( resourceID + ": " + stepError ).c_str() );
                    continue;
                }
                ++inoutTally._validatedStepCount;
                if ( sw::AutomationStepRegistry::find( step._kind ) == nullptr )
                    ++inoutTally._validatorOnlyStepCount;
                if ( bStandIn )
                    ++inoutTally._standInStepCount;
            }
            return bFullySeen;
        }
    };
} // namespace

/**
 * @brief [AutomationScenarioDataTest] 모든 시나리오의 단계가 등록된 검사 함수를 통과한다 - 앱을 띄우기 전에 속성 이름 · 값 · 필수 속성 오류를 잡는다
 * @details 실행기는 시작 프레임에서야 단계를 검사해 GPU 검증까지 가야 오류가 드러났다. 검사 함수(`AutomationStepRegistration::_pValidate`)는 순수 함수라 실행기 없이 부른다.
 *          에디터 · App 의 단계는 검사 함수만 따로 올라와 있어(`SW_AUTOMATION_STEP_VALIDATOR`) 그 모듈 없이도 본다. 에디터 · 게임 모듈의 탐침 · 개발 명령은 이름만 받아 주는
 *          대역으로 올려 속성 검사를 이어 간다(이름 · 탐침의 `target` · `key` 요구는 린트). `Variable` 의 변수 이름도 대역으로 받고 `ExpectVariable` 은 값 타입을 알아야 해서 못 본 단계로 남으며,
 *          합계와 이유는 로그에 적는다. 시나리오를 훑으므로 새 시나리오는 자동으로 들어온다.
 *          준비 함수(`_pPrepare`)만 보는 것은 이 테스트가 보지 않는다 - `Action` 의 입력 맵 액션 이름, 누를 수 없는 슬롯(축), `EditorKey` 의 ImGui 키 이름, `EditorExpectObject` 의 컴포넌트 타입 이름, `MousePosition` 의 창 필요 여부.
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
    {
        AutomationScenarioDataInternal::StandIns standIn;
        for ( const sw::string& filePath : listFilePath )
        {
            const sw::string resourceID = sw::ResourcePaths::toResourceID( filePath );
            if ( AutomationScenarioDataInternal::isScenarioFile( resourceID ) == false )
                continue;
            ++tally._scenarioCount;
            if ( AutomationScenarioDataInternal::validateScenario( sw::ResourceID{ resourceID }, standIn, tally ) )
                ++tally._fullySeenScenarioCount;
        }
    }

    SW_LOG_INFO( "[ScenarioData] %# scenarios, %# fully validated, %# partly; %# steps: %# validated (%# by a validator without its module, %# with stand-in probe/command names), %# without a validator, %# not seen",
                 tally._scenarioCount, tally._fullySeenScenarioCount, tally._scenarioCount - tally._fullySeenScenarioCount, tally._stepCount, tally._validatedStepCount,
                 tally._validatorOnlyStepCount, tally._standInStepCount, tally._noValidatorStepCount, tally._unseenStepCount );
    for ( const auto& unseen : tally._mapUnseenReasonToStepCount )
    {
        SW_LOG_INFO( "[ScenarioData] not seen, %# step(s) - %#", unseen.second, unseen.first );
    }

    // 폴더를 못 찾아 0 편을 훑거나, 모든 단계가 못 본 단계로 빠지면 이 테스트는 아무것도 보지 않은 것이다.
    SW_EXPECT_TRUE_MSG( tally._scenarioCount >= 10u, ( "훑은 시나리오가 " + std::to_string( tally._scenarioCount ) + " 편뿐입니다" ).c_str() );
    SW_EXPECT_TRUE_MSG( tally._validatedStepCount > 0u, "검사 함수를 부른 단계가 하나도 없습니다 - 엔진 단계가 등록되지 않았습니다" );
    SW_EXPECT_TRUE_MSG( tally._validatorOnlyStepCount > 0u, "단계 등록 없이 검사 함수만 올라온 단계(에디터 · App)를 하나도 보지 못했습니다 - SW_AUTOMATION_STEP_VALIDATOR 가 링크되지 않았습니다" );
}
