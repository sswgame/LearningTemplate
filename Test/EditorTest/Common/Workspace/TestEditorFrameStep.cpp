#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"

#include "Editor/Common/Workspace/EditorFrameStep.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    using FrameStepRegistry  = EditorRegistry<EditorFrameStepRegistration>;
    using FrameStepRegistrar = EditorRegistrar<EditorFrameStepRegistration>;

    // 테스트 단계는 부르지 않는다(EditorTest 는 EditorContext 를 만들 수 없다). 단계가 무엇인지는 함수 주소로 가린다.
    void runFirstStep( EditorContext& /*context*/ ) {}
    void runSecondStep( EditorContext& /*context*/ ) {}
    void runReloadedStep( EditorContext& /*context*/ ) {}

    /** @brief 프레임 단계 목록의 id 를 셸이 부를 순서대로 쉼표로 이은 글입니다. EditorTest 에는 셸의 단계(`EditorFrameSteps.cpp`)가 링크되지 않는다. */
    string joinFrameStepIDs()
    {
        string text;
        for ( uint32 index = 0; index < FrameStepRegistry::getCount(); ++index )
        {
            if ( text.empty() == false )
                text += ',';
            text += FrameStepRegistry::getAt( index )._pID;
        }
        return text;
    }
} // namespace

/**
 * @brief [EditorFrameStepTest] 셸이 아는 단계의 순서 값은 단축키 → 에셋 리로드 → 패널 → 자체 테스트 → 창 제목 → 스크린샷이다
 * @details 순서에 뜻이 있다. 에셋 리로드가 패널보다 앞이어야 이번 프레임에 바뀐 머티리얼이 보이고, 자체 테스트가 패널 뒤여야 이번 프레임의 패널 상태를 본다.
 */
SW_TEST_CASE( EditorFrameStepTest, ShellStepOrderKeepsItsMeaning )
{
    SW_EXPECT_TRUE( EditorFrameStepOrder::kCommands < EditorFrameStepOrder::kAssetReload );
    SW_EXPECT_TRUE( EditorFrameStepOrder::kAssetReload < EditorFrameStepOrder::kPanels );
    SW_EXPECT_TRUE( EditorFrameStepOrder::kPanels < EditorFrameStepOrder::kSelfTest );
    SW_EXPECT_TRUE( EditorFrameStepOrder::kSelfTest < EditorFrameStepOrder::kWindowTitle );
    SW_EXPECT_TRUE( EditorFrameStepOrder::kWindowTitle < EditorFrameStepOrder::kScreenshot );
}

/**
 * @brief [EditorFrameStepTest] 단계는 등록 순서가 아니라 `_order` 순서로 돌고, 같은 값이면 id 사전순이다
 * @details 단계는 각자의 .cpp 에서 정적 등록자로 오르고, 번역 단위 사이의 정적 초기화 순서는 정해지지 않는다.
 */
SW_TEST_CASE( EditorFrameStepTest, OrderDecidesRunOrderNotRegistrationOrder )
{
    SW_ASSERT_EQUAL( 0u, FrameStepRegistry::getCount() );
    {
        const FrameStepRegistrar panels{
            EditorFrameStepRegistration{ { "panels", EditorFrameStepOrder::kPanels }, &runSecondStep }
        };
        const FrameStepRegistrar assetReload{
            EditorFrameStepRegistration{ { "assetReload", EditorFrameStepOrder::kAssetReload }, &runFirstStep }
        };
        const FrameStepRegistrar selfTest{
            EditorFrameStepRegistration{ { "selfTest", EditorFrameStepOrder::kSelfTest }, &runFirstStep }
        };
        // 확장 모듈은 사이 값으로 끼어든다.
        const FrameStepRegistrar extension{
            EditorFrameStepRegistration{ { "extension", EditorFrameStepOrder::kPanels + 50 }, &runSecondStep }
        };

        SW_EXPECT_STREQ( "assetReload,panels,extension,selfTest", joinFrameStepIDs().c_str() );
        SW_EXPECT_TRUE( FrameStepRegistry::getAt( 1 )._pfnRun == &runSecondStep );
    }
    SW_EXPECT_EQUAL( 0u, FrameStepRegistry::getCount() );
}

/**
 * @brief [EditorFrameStepTest] 같은 id 의 둘째 단계는 거절되어 한 프레임에 두 번 돌지 않는다
 */
SW_TEST_CASE( EditorFrameStepTest, SecondStepWithSameIDIsRejected )
{
    const FrameStepRegistrar first{
        EditorFrameStepRegistration{ { "panels", EditorFrameStepOrder::kPanels }, &runFirstStep }
    };
    SW_ASSERT_TRUE( first.isRegistered() );
    {
        test::ScopedDefensiveTestLog expected( "a second frame step with one id" );
        const FrameStepRegistrar     second{
            EditorFrameStepRegistration{ { "panels", EditorFrameStepOrder::kCommands }, &runSecondStep }
        };
        SW_EXPECT_FALSE( second.isRegistered() );
        SW_EXPECT_EQUAL( 1u, FrameStepRegistry::getCount() );
        SW_EXPECT_TRUE( FrameStepRegistry::getAt( 0 )._pfnRun == &runFirstStep );
    }
    SW_EXPECT_EQUAL( 1u, FrameStepRegistry::getCount() );
    SW_EXPECT_TRUE( FrameStepRegistry::find( "panels" ) == &first.getRegistration() );
}

/**
 * @brief [EditorFrameStepTest] 핫 리로드가 올린 새 이미지의 단계는 옛 것을 제자리에서 대체하고, 옛 이미지가 내려가도 남는다
 * @details 새 이미지의 정적 등록자는 옛 이미지가 아직 도는 동안 돈다. 거절되면 옛 이미지가 내려간 뒤 그 단계가 프레임에서 사라진다.
 */
SW_TEST_CASE( EditorFrameStepTest, ReloadReplacesStepInPlace )
{
    const FrameStepRegistrar commands{
        EditorFrameStepRegistration{ { "commands", EditorFrameStepOrder::kCommands }, &runFirstStep }
    };
    const FrameStepRegistrar selfTest{
        EditorFrameStepRegistration{ { "selfTest", EditorFrameStepOrder::kSelfTest }, &runFirstStep }
    };
    // 옛 이미지의 단계
    unique_ptr<FrameStepRegistrar> pOldImage = make_unique<FrameStepRegistrar>( EditorFrameStepRegistration{
        { "panels", EditorFrameStepOrder::kPanels },
        &runSecondStep
    } );
    unique_ptr<FrameStepRegistrar> pNewImage{};
    {
        const RegistrationReloadScope reloadScope;
        pNewImage = make_unique<FrameStepRegistrar>( EditorFrameStepRegistration{
            { "panels", EditorFrameStepOrder::kPanels },
            &runReloadedStep
        } );
    }
    SW_EXPECT_TRUE( pNewImage->isRegistered() );
    SW_EXPECT_STREQ( "commands,panels,selfTest", joinFrameStepIDs().c_str() );
    SW_EXPECT_TRUE( FrameStepRegistry::getAt( 1 )._pfnRun == &runReloadedStep );

    pOldImage.reset(); // 옛 이미지가 내려간다
    SW_EXPECT_STREQ( "commands,panels,selfTest", joinFrameStepIDs().c_str() );
    SW_EXPECT_TRUE_MSG( FrameStepRegistry::find( "panels" ) == &pNewImage->getRegistration(), "the reloaded frame step is lost when the old image unloads" );

    pNewImage.reset();
    SW_EXPECT_STREQ( "commands,selfTest", joinFrameStepIDs().c_str() );
}
