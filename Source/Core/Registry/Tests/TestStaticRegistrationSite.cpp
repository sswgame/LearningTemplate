// StaticRegistrationTest — 겹친 정적 등록은 두 자리(파일 · 줄)를 찍고 시작 단계가 로드 오류로 멈추게 센다. 실행 중 손으로 부른 등록의 겹침은 세지 않는다.
#include "pch.h"

#include "Core/Process/StandardOutput.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 이름만 든 등록 대상입니다. */
    struct DuplicateProbe
    {
        const utf8* _pName;
    };

    /** @brief 이 테스트만의 정적 등록 표입니다(이름 필수). */
    struct DuplicateProbeRegistry
    {
        static bool add( const DuplicateProbe* pProbe ) { return getList().addLogged( pProbe, "Duplicate probe", pProbe->_pName ) == RegistrationResult::Added; }
        static void remove( const DuplicateProbe* pProbe ) { (void)getList().remove( pProbe ); }

        static SW_DEFINE_REGISTRATION_LIST( getList, const DuplicateProbe, "DuplicateProbeRegistry", RegistrationOrder::Insertion, true )
    };
} // namespace

/**
 * @brief 자식 역할: 같은 이름의 정적 등록자 둘을 세우고, 거절된 정적 등록 줄(시작 단계가 로드 오류로 읽는 값)을 표식과 함께 찍는다.
 * @details 정적 등록 거절 수는 프로세스 전역이라 자식에서 센다 — 이 실행의 다른 케이스 · 시나리오가 그 수를 보지 않게.
 */
SW_TEST_CHILD_ROLE( StaticRegistrationTest, DuplicateRegistrarChild )
{
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    SW_EXPECT_TRUE( collectRejectedStaticRegistrations().empty() ); // 이 실행 파일의 기동에는 겹친 정적 등록이 없다(있으면 `App --test` 가 시작하지 않았다)
    const DuplicateProbe                         first{ "probe" };
    const DuplicateProbe                         second{ "probe" };
    const StaticRegistrar<const DuplicateProbe*> firstRegistrar{
        &first, &DuplicateProbeRegistry::add, &DuplicateProbeRegistry::remove, SourceLocation{ "first_site.cpp", 10 }
    };
    const StaticRegistrar<const DuplicateProbe*> secondRegistrar{
        &second, &DuplicateProbeRegistry::add, &DuplicateProbeRegistry::remove, SourceLocation{ "second_site.cpp", 20 }
    };
    const vector<string> listRejected = collectRejectedStaticRegistrations();
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listRejected.size() ) );
    SW_EXPECT_TRUE( DuplicateProbeRegistry::getList().findByName( "probe" ) == &first ); // 먼저 것을 둔다
    StringBuilder<constant::buffer::kMax512> output;
    output.appendFormat( "SW_REJECTED:%#\n", listRejected.front() );
    SW_EXPECT_TRUE( StandardOutput::write( output.view() ) );
}

/**
 * @brief [StaticRegistrationTest] 겹친 정적 등록은 두 자리를 함께 찍은 줄로 남는다 — 시작 단계(`TestRunner::run` · 시나리오 단계)가 이 줄이 있으면 로드 오류로 멈춘다
 * @details 예전에는 오류 로그 한 줄만 남고 뒤의 등록이 조용히 빠졌다 — 두 갈래가 같은 감시 값을 더한 것이 전체 검증 중간에야 드러났다.
 */
SW_TEST_CASE( StaticRegistrationTest, DuplicateStaticRegistrationIsALoadErrorWithBothSites, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const test::ChildRunResult child = test::runChildRole( "StaticRegistrationTest.DuplicateRegistrarChild", {}, 120.0f );
    SW_REQUIRE_TRUE_MSG( child._exitCode.has_value(), child.getOutputTail() );
    SW_EXPECT_TRUE_MSG( *child._exitCode == 0, child.getOutputTail() );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( child._output, "SW_REJECTED:" ), child.getOutputTail() );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( child._output, "first_site.cpp(10)" ), child.getOutputTail() );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( child._output, "second_site.cpp(20)" ), child.getOutputTail() );
}

/** @brief [StaticRegistrationTest] 실행 중 손으로 부른 등록이 겹치면(테스트가 일부러 겹치는 것) 오류 로그는 남지만 정적 등록 거절로 세지 않는다 — 시작 검사를 흔들지 않는다 */
SW_TEST_CASE( StaticRegistrationTest, RuntimeDuplicateIsNotCountedAsStatic )
{
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    const size_t         before = collectRejectedStaticRegistrations().size();
    const DuplicateProbe first{ "runtime" };
    const DuplicateProbe second{ "runtime" };
    SW_EXPECT_TRUE( DuplicateProbeRegistry::add( &first ) );
    SW_EXPECT_FALSE( DuplicateProbeRegistry::add( &second ) );
    SW_EXPECT_EQUAL( before, collectRejectedStaticRegistrations().size() );
    DuplicateProbeRegistry::remove( &first );
}
