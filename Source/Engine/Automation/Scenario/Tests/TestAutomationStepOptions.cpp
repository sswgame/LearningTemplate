#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationRunnerParams.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"

using namespace sw;

namespace
{
    struct AutomationStepOptionsTestUtil
    {
        static bool               run( IAutomationStepContext&, const AutomationStep& ) { return true; }
        static expected<void>     prepare( IAutomationStepContext&, const AutomationStep& ) { return {}; }
        static AutomationStepHold hold( IAutomationStepContext&, const AutomationStep&, InputManager& ) { return AutomationStepHold::Run; }
    };
} // namespace

/**
 * @brief [AutomationStepOptionsTest] 선택 항목을 적지 않으면 틱 뒤에 도는 평범한 단계이고, 적은 항목은 다른 칸을 건드리지 않고 등록 칸으로 가는지 검증
 */
SW_TEST_CASE( AutomationStepOptionsTest, OptionsReachTheirRegistrationProperties )
{
    using Util = AutomationStepOptionsTestUtil;

    const AutomationStepRegistration plain = AutomationStepRegistration::make( "Plain", &Util::run, nullptr, AutomationStepOptions{} );
    SW_EXPECT_TRUE( plain._pPrepare == nullptr );
    SW_EXPECT_TRUE( plain._pHold == nullptr );
    SW_EXPECT_FALSE( plain._bBeforeInput );
    SW_EXPECT_FALSE( plain._bUsesDesktop );
    SW_EXPECT_NEAR( 0.0f, plain._wallSeconds, 0.0f );

    const AutomationStepRegistration full = AutomationStepRegistration::make( "Full", &Util::run, nullptr, AutomationStepOptions{}.withPrepare( &Util::prepare ).withHold( &Util::hold ).withBeforeInput().withDesktop().withWallBudget( 7.0f ) );
    SW_EXPECT_TRUE( full._pPrepare == &Util::prepare );
    SW_EXPECT_TRUE( full._pHold == &Util::hold );
    SW_EXPECT_TRUE( full._bBeforeInput );
    SW_EXPECT_TRUE( full._bUsesDesktop );
    SW_EXPECT_NEAR( 7.0f, full._wallSeconds, 0.0f );

    const AutomationStepRegistration waitOnly = AutomationStepRegistration::make( "Wait", nullptr, nullptr, AutomationStepOptions{}.withHold( &Util::hold ) );
    SW_EXPECT_TRUE( waitOnly.isWaitOnly() );
    SW_EXPECT_FALSE( full.isWaitOnly() );
}

/**
 * @brief [AutomationStepOptionsTest] 스키마 검사 함수가 모르는 속성을 거절하고 스키마에 맞는 단계를 통과시키는지 검증
 */
SW_TEST_CASE( AutomationStepOptionsTest, ValidatePropertiesFollowsTheSchema )
{
    AutomationStep step;
    step._kind = "Pass";
    SW_EXPECT_TRUE( AutomationStep::validateProperties<AutomationNoParams>( step ).has_value() );

    step._listAttribute.push_back( { "unknown", "1" } );
    const expected<void> rejected = AutomationStep::validateProperties<AutomationNoParams>( step );
    SW_REQUIRE_FALSE( rejected.has_value() );
    SW_EXPECT_TRUE( rejected.error().detail().find( "unknown" ) != string_view::npos );
}

/**
 * @brief [AutomationStepOptionsTest] `<Screenshot stability>` 는 열거자 철자 그대로만 받고, 적지 않으면 Render 다 — 모르는 값은 읽기에서 진다
 */
SW_TEST_CASE( AutomationStepOptionsTest, ScreenshotStabilityReadsEnumeratorNames )
{
    AutomationStep step;
    step._kind = "Screenshot";
    step._listAttribute.push_back( { "file", "a.ppm" } );
    AutomationScreenshotParams params;
    SW_REQUIRE_TRUE( step.readProperties( params ).has_value() );
    SW_EXPECT_TRUE( params._stability == AutomationScreenshotStability::Render );

    step._listAttribute.push_back( { "stability", "None" } );
    SW_REQUIRE_TRUE( step.readProperties( params ).has_value() );
    SW_EXPECT_TRUE( params._stability == AutomationScreenshotStability::None );

    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" );
    step._listAttribute.back()._value = "assets";
    SW_EXPECT_FALSE( step.readProperties( params ).has_value() );
}
