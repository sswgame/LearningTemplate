#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Process/ProcessEnvironment.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/NetEmulationPresetTable.h"

#include "EngineTest/NetSimDestructionScenario.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

// 루프백 클러스터 부하 — 서버 하나와 클라이언트 여럿이 나쁜 회선에서 파괴 쇼케이스를 돌리는 동안의 틱 시간과, 끝에 모든 클라이언트의 구조 해시가 서버와 같은지(어긋남 없음).
// 부하 검증(`py -3 -m Scripts load`)이 시나리오의 `<Load>` 를 환경 변수로 넘긴다. 없으면 아래 기본값으로 돈다(`ctest -L load`).
//   SW_LOAD_CLIENTS=<n>           클라이언트 수(기본 8)
//   SW_LOAD_RAMP_UP_SECONDS=<s>   이 시간에 걸쳐 클라이언트를 늘린다(기본 2)
//   SW_LOAD_DURATION_SECONDS=<s>  창(60 Hz 틱, 기본 12) — 그 뒤 모두가 서버와 같아질 때까지 상한 안에서 더 돈다
//   SW_LOAD_NETWORK=<프리셋>      off · lan · wifi · lte · badwifi(기본 lte)
// 결과는 `[LoadMetric] <이름>=<값>` 줄로 남긴다 — `Scripts/qa/Load.py` 가 읽어 예산 · 기준선과 비교한다.

SW_TEST_LOAD_SUITE( NetSimLoadTest, "서버 + 클라이언트 여럿의 틱 시간과 부하 중 상태 해시 일치를 잰다(부하 검증의 cluster 드라이버)" );
SW_TEST_TIME_BUDGET( NetSimLoadTest, 10500, "Release 케이스 최대 2517 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

using namespace test;

namespace
{
    struct NetSimLoadInternal
    {
        static constexpr float32 kTickRate = 60.0f;

        /** @brief 환경 변수의 숫자입니다. 없거나 비면 @p fallback(문서의 기본값)이고, 값이 있는데 숫자가 아니면 false 입니다. */
        [[nodiscard]] static bool tryReadEnvironmentNumber( const utf8* pName, float32 fallback, float32& outValue )
        {
            sw::string valueText;
            outValue = fallback;
            if ( sw::ProcessEnvironment::tryReadNonEmpty( pName, valueText ) == false )
                return true;
            return sw::StringUtil::parseFloat( valueText, outValue );
        }
    };
} // namespace

/**
 * @brief [NetSimLoadTest] 클라이언트 여럿 · 나쁜 회선에서 파괴 복제가 끝까지 서버와 같은 구조 해시로 수렴하고, 틱 시간 분포를 남긴다
 */
SW_TEST_CASE( NetSimLoadTest, DestructionReplicationUnderLoad )
{
    using Internal = NetSimLoadInternal;
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );

    sw::string        pNetworkText;
    const utf8*       pNetwork = sw::ProcessEnvironment::tryRead( "SW_LOAD_NETWORK", pNetworkText ) ? pNetworkText.c_str() : nullptr;
    const string_view network  = pNetwork != nullptr && pNetwork[0] != '\0' ? string_view( pNetwork ) : string_view( "lte" );
    ScenarioOptions   options;
    float32           clientCount     = 0.0f;
    float32           rampUpSeconds   = 0.0f;
    float32           durationSeconds = 0.0f;
    SW_ASSERT_TRUE_MSG( Internal::tryReadEnvironmentNumber( "SW_LOAD_CLIENTS", 8.0f, clientCount ), "SW_LOAD_CLIENTS is not a number" );
    SW_ASSERT_TRUE_MSG( Internal::tryReadEnvironmentNumber( "SW_LOAD_RAMP_UP_SECONDS", 2.0f, rampUpSeconds ), "SW_LOAD_RAMP_UP_SECONDS is not a number" );
    SW_ASSERT_TRUE_MSG( Internal::tryReadEnvironmentNumber( "SW_LOAD_DURATION_SECONDS", 12.0f, durationSeconds ), "SW_LOAD_DURATION_SECONDS is not a number" );
    options._clientCount = static_cast<uint32>( MathUtil::max( 1.0f, clientCount ) );
    options._rampUpTicks = static_cast<uint32>( rampUpSeconds * Internal::kTickRate );
    options._tickCount   = static_cast<uint32>( MathUtil::max( 1.0f, durationSeconds ) * Internal::kTickRate );
    NetEmulationPresetTable presetTable;
    SW_ASSERT_TRUE( presetTable.loadFromResource( sw::ResourceID{ ::test::getEngineDefaultAssets()._netEmulationPresets.get() } ) );
    SW_ASSERT_TRUE_MSG( presetTable.tryGetConditions( network, options._conditions ), string( "unknown SW_LOAD_NETWORK preset " ) + string( network ) );

    const ScenarioResult result = runShowcase( document, options );
    logResult( "load", result );
    SW_ASSERT_TRUE( result._bValid );
    SW_EXPECT_TRUE_MSG( result._convergedTick >= 0, "every client ends with the server's structure hash under load" );
    SW_EXPECT_TRUE( result._serverEventCount > 3 );

    SW_LOG_INFO( "[LoadMetric] clients=%#", options._clientCount );
    SW_LOG_INFO( "[LoadMetric] tick_p50_us=%#", computePercentile( result._listStepMicro, 0.5f ) );
    SW_LOG_INFO( "[LoadMetric] tick_p99_us=%#", computePercentile( result._listStepMicro, 0.99f ) );
    SW_LOG_INFO( "[LoadMetric] tick_max_us=%#", computePercentile( result._listStepMicro, 1.0f ) );
    SW_LOG_INFO( "[LoadMetric] ticks=%#", result._listStepMicro.size() );
    SW_LOG_INFO( "[LoadMetric] converged=%#", result._convergedTick >= 0 ? 1 : 0 );
    SW_LOG_INFO( "[LoadMetric] hash_mismatch=%#", result._hashMismatchCount );
    SW_LOG_INFO( "[LoadMetric] server_up_bytes_per_second=%#", result._serverUploadBytesPerSecond );
}
