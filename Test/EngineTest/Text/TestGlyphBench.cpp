// 글리프 벤치 — 한글 장문을 처음 띄우는 프레임의 CPU 몫(새 글리프 수백 개의 글꼴 찾기 · 래스터화 · SDF · 아틀라스 자리 잡기). 값은 Release 로 읽는다.
// GPU 업로드 몫은 App 탐침(`-gv_benchUiHangulGlyphs=N`, 표의 `RT.Canvas.Upload` max)으로 잰다.
#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Text/FontService.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/SystemFontLocator.h"

#include "EngineTest/LocalizationTestUtil.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

// 시스템 한글 글꼴(malgun · Noto CJK)을 읽는다 — CI 러너 이미지에는 없다. 측정 탐침이라 호스트 스위트로 둔다(`--host_suites=only`).
SW_TEST_REQUIRES_HOST( GlyphBenchTest, "rasterizes a system Korean font - CI images have none" );

namespace
{
    struct GlyphBenchInternal
    {
        static constexpr uint32 kHangulFirst = 0xAC00u; ///< '가'
        static constexpr uint32 kHangulCount = 11172u;  ///< 현대 한글 음절 수
        /// 음절을 고르는 보폭이다. 음절 수와 서로소라 판마다 다른 음절이 나오고 앞 판의 캐시에 걸리지 않는다.
        static constexpr uint32 kHangulStride  = 37u;
        static constexpr uint32 kGlyphPerRound = 400u;
        static constexpr uint32 kRoundCount    = 5u;

        static bool hasKoreanSystemFont()
        {
            return sw::SystemFontLocator::findSystemFontFile( "malgun.ttf" ).empty() == false ||
                   sw::SystemFontLocator::findSystemFontFile( "NotoSansKR-Regular.ttf" ).empty() == false ||
                   sw::SystemFontLocator::findSystemFontFile( "NotoSansCJK-Regular.ttc" ).empty() == false;
        }
    };
} // namespace

/**
 * @brief [GlyphBenchTest] 새 한글 글리프 400 개를 한 프레임에 처음 그릴 때의 CPU 시간(다섯 판, 판마다 다른 음절) — 시스템 한글 글꼴이 없으면 건너뛴다
 * @details 판마다 같은 FontService 를 쓰되 음절이 겹치지 않으므로 모두 새로 래스터화한다(글꼴 열기는 첫 판 앞에서 끝낸다).
 *          판정하지 않는다(측정값). p50 이 4 ms 를 넘으면 SDF 를 컴퓨트(점프 플러딩)로 만들지 정한다(백로그).
 */
SW_TEST_CASE( GlyphBenchTest, HangulFirstFrameRasterize )
{
    if ( GlyphBenchInternal::hasKoreanSystemFont() == false )
        SW_TEST_SKIP( "no Korean system font on this machine" );

    using sw::test::LocalizationTestUtil;
    const sw::string folder      = test::makeTempDirectory( "glyph_bench_hangul" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])" );
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.title": { "source": "Title" })" );
    LocalizationTestUtil::writeTranslation( folder, "ko", R"("ui.title": { "text": "제목" })" );
    sw::LocalizationManager localization;
    SW_ASSERT_TRUE( LocalizationTestUtil::loadEngineCultures( localization ) );
    SW_ASSERT_TRUE( localization.mountProject( projectPath, sw::LocalizationScope::Game ) );
    SW_ASSERT_TRUE( localization.setCurrentLanguage( "ko" ) );

    sw::FontService fontService( sw::IFontRasterizer::createDefault(), &localization );
    SW_TEST_DEFENSIVE_SCOPE( "fallback families not installed on this machine warn once" );
    SW_ASSERT_TRUE( fontService.initialize( test::getEngineDefaultAssets()._fontCatalog.get() ) );
    const sw::FontFaceChain chain = fontService.getFaceChain( sw::FontSpec{} );
    // 대체 면을 미리 연다 — 판이 재는 것은 글리프마다의 일이지 글꼴 파일 열기가 아니다.
    uint32 warmGlyphIndex = 0;
    (void)fontService.findFaceForCodepoint( chain, GlyphBenchInternal::kHangulFirst, warmGlyphIndex );

    sw::vector<int64> listMicros;
    uint32            codepointCursor = 1;
    for ( uint32 round = 0; round < GlyphBenchInternal::kRoundCount; ++round )
    {
        uint32              rasterizedCount = 0;
        const sw::Stopwatch stopwatch;
        for ( uint32 index = 0; index < GlyphBenchInternal::kGlyphPerRound; ++index, ++codepointCursor )
        {
            const uint32         codepoint  = GlyphBenchInternal::kHangulFirst + ( codepointCursor * GlyphBenchInternal::kHangulStride ) % GlyphBenchInternal::kHangulCount;
            uint32               glyphIndex = 0;
            const sw::FontFaceID face       = fontService.findFaceForCodepoint( chain, codepoint, glyphIndex );
            if ( fontService.getGlyphCache().findOrAddGlyph( face, glyphIndex, round + 1u ) != nullptr )
                ++rasterizedCount;
        }
        const int64 micros = stopwatch.getElapsedNanoseconds() / 1000;
        listMicros.push_back( micros );
        // 아틀라스가 한 프레임에 다 못 받으면 일부가 nullptr 이다(그 자체가 측정할 사실이라 판정하지 않고 하나 이상만 본다).
        SW_EXPECT_TRUE( rasterizedCount > 0 );
    }
    test::logBenchSamples( "Hangul 400 new glyphs, CPU rasterize + SDF + atlas (us)", listMicros );
}
