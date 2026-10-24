#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/AutomationImageMetric.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/CookContract.gen.h"

#include <cstdlib>

// 실제 App.exe 를 백엔드마다 띄워 UI 견본 화면의 스크린샷을 읽는다 — GPU · 창 · 셰이더가 필요하다. CI 러너엔 없다.
SW_TEST_REQUIRES_HOST( AppUITest, "launches App.exe on every RHI backend and reads screenshots of the UI demo screen" );

// ------------------------------------------------------------------------------
// AppUITest — 런타임 UI 를 네 백엔드로 그려 화면을 비교한다
//
// 엔진 시나리오 `engine/automation/uidemo.scenario.xml`(가상 입력 exclusive — 사람 마우스가 호버를 바꾸지 않는다)을 백엔드마다 돌려
// 스크린샷(PPM)과 레이아웃 덤프(`UILayoutDump` 단계 — 위젯 이름 → 물리 픽셀 사각형)를 받는다. 덤프로 견본의 사각형을 찾아
// (1) 알려진 영역을 단언하고 — 주 단추 가운데는 강조색(파랑), 둥근 상자 모서리 바깥은 패널 바탕, 자르기 상자 밖은 잘린 빨강이 아니다 —
// (2) 영역마다 첫 백엔드와 화소 단위로 비교한다(다른 화소 비율 ≤ kMaxDifferingFraction, 가장자리 수 차 ≤ 2 %). 알려진 영역의 단언은 색 차 · 대소로 본다(톤맵 · 클리어 색에 버틴다).
// 종료 코드 13 · 77 은 건너뛴다(AppScenarioTest 와 같은 규칙).
// ------------------------------------------------------------------------------

namespace
{
    struct AppUITestRect
    {
        float32 _x{ 0.0f };
        float32 _y{ 0.0f };
        float32 _width{ 0.0f };
        float32 _height{ 0.0f };
    };

    struct AppUITestColor
    {
        float32 _r{ 0.0f };
        float32 _g{ 0.0f };
        float32 _b{ 0.0f };
    };

    /** @brief 백엔드 하나의 산출물 — 스크린샷과 견본 위젯 이름 → 그림 픽셀 사각형입니다. */
    struct AppUICapture
    {
        sw::string                                   _backend{};
        sw::AutomationImage                          _image{};
        sw::unordered_map<sw::string, AppUITestRect> _mapRectByName{};
    };

    struct AppUITestInternal
    {
        static constexpr const utf8* kScenarioPath = "engine/automation/uidemo.scenario.xml";
        static constexpr const utf8* kOutputFolder = "Saved/Automation/engine.uidemo";
        /// 백엔드 사이 영역에서 다른 화소(`AutomationImageMetric::kDifferingMinDistance` 이상)의 비율 상한 — 영역 평균 색 차이는 글자 몇 개 ·
        /// 모서리 하나가 틀린 것을 영역 넓이로 나눠 묻으므로 화소를 센다. 2026-10 Release 네 백엔드 실측은 여섯 영역 모두 0 이다.
        static constexpr float64     kMaxDifferingFraction = 0.001;
        static constexpr float32     kEdgeTolerance        = 0.02f; ///< 백엔드 사이 가장자리 수 차(비율)
        static constexpr float32     kEdgeThreshold        = 0.12f; ///< 이웃 휘도 차가 이보다 크면 가장자리 픽셀
        static constexpr float32     kMsdfLumaTolerance    = 0.1f;  ///< MSDF 와 SDF 패널 평균 휘도의 상대 차 상한(글자가 사라지거나 번지면 벌어진다)
        static constexpr float32     kMsdfEdgeTolerance    = 0.15f; ///< MSDF 와 SDF 패널 가장자리 수의 상대 차 상한
        static constexpr const utf8* kArrRegionName[]      = { "DemoPanel", "Start", "RoundBox", "ClipBox", "NineSlice", "RtlSample" };

        /**
         * @brief 레이아웃 덤프에서 코드 화면(`## (code)` — 견본 화면) 절의 위젯 이름 → 사각형을 읽어 그림 픽셀로 맞춥니다(루트 = 그림 크기).
         * @return 절을 못 찾으면 false
         */
        [[nodiscard]] static bool parseDemoRects( const sw::string& text, uint32 imageWidth, uint32 imageHeight, sw::unordered_map<sw::string, AppUITestRect>& outMap )
        {
            const size_t sectionStart = text.find( "## (code)\n" );
            if ( sectionStart == sw::string::npos )
                return false;
            size_t  lineStart = sectionStart + 10;
            float32 scaleX    = 1.0f;
            float32 scaleY    = 1.0f;
            bool    bRoot     = true;
            while ( lineStart < text.size() && text.compare( lineStart, 3, "## " ) != 0 )
            {
                size_t lineEnd = text.find( '\n', lineStart );
                if ( lineEnd == sw::string::npos )
                    lineEnd = text.size();
                const sw::string line                  = text.substr( lineStart, lineEnd - lineStart );
                lineStart                              = lineEnd + 1;
                const sw::vector<sw::string> listToken = splitWords( line );
                if ( listToken.size() != 5 )
                    continue; // collapsed
                AppUITestRect rect{};
                rect._x      = static_cast<float32>( std::atof( listToken[1].c_str() ) );
                rect._y      = static_cast<float32>( std::atof( listToken[2].c_str() ) );
                rect._width  = static_cast<float32>( std::atof( listToken[3].c_str() ) );
                rect._height = static_cast<float32>( std::atof( listToken[4].c_str() ) );
                if ( bRoot )
                {
                    bRoot  = false;
                    scaleX = rect._width > 0.0f ? static_cast<float32>( imageWidth ) / rect._width : 1.0f;
                    scaleY = rect._height > 0.0f ? static_cast<float32>( imageHeight ) / rect._height : 1.0f;
                }
                rect._x *= scaleX;
                rect._width *= scaleX;
                rect._y *= scaleY;
                rect._height *= scaleY;
                outMap.emplace( listToken[0], rect );
            }
            return bRoot == false;
        }

        /** @brief 공백으로 나눈 낱말들입니다(덤프 줄 — 들여쓰기는 버린다). */
        static sw::vector<sw::string> splitWords( const sw::string& line )
        {
            sw::vector<sw::string> listWord;
            size_t                 index = 0;
            while ( index < line.size() )
            {
                while ( index < line.size() && line[index] == ' ' )
                {
                    ++index;
                }
                const size_t start = index;
                while ( index < line.size() && line[index] != ' ' )
                {
                    ++index;
                }
                if ( index > start )
                    listWord.push_back( line.substr( start, index - start ) );
            }
            return listWord;
        }

        static AppUITestColor readPixel( const sw::AutomationImage& image, int32 x, int32 y )
        {
            x                   = sw::MathUtil::clamp( x, 0, static_cast<int32>( image._width ) - 1 );
            y                   = sw::MathUtil::clamp( y, 0, static_cast<int32>( image._height ) - 1 );
            const size_t offset = ( static_cast<size_t>( y ) * image._width + static_cast<size_t>( x ) ) * 3;
            return AppUITestColor{ image._listRgb[offset] / 255.0f, image._listRgb[offset + 1] / 255.0f, image._listRgb[offset + 2] / 255.0f };
        }

        /** @brief 사각형 안(가장자리 @p inset 픽셀 빼고) 평균 색입니다. */
        static AppUITestColor computeMean( const sw::AutomationImage& image, const AppUITestRect& rect, int32 inset = 0 )
        {
            AppUITestColor sum{};
            uint32         count = 0;
            const int32    x0    = static_cast<int32>( rect._x ) + inset;
            const int32    y0    = static_cast<int32>( rect._y ) + inset;
            const int32    x1    = static_cast<int32>( rect._x + rect._width ) - inset;
            const int32    y1    = static_cast<int32>( rect._y + rect._height ) - inset;
            for ( int32 y = y0; y < y1; ++y )
            {
                for ( int32 x = x0; x < x1; ++x )
                {
                    const AppUITestColor pixel = readPixel( image, x, y );
                    sum._r += pixel._r;
                    sum._g += pixel._g;
                    sum._b += pixel._b;
                    ++count;
                }
            }
            if ( count != 0 )
                sum = AppUITestColor{ sum._r / static_cast<float32>( count ), sum._g / static_cast<float32>( count ), sum._b / static_cast<float32>( count ) };
            return sum;
        }

        static float32 computeLuma( const AppUITestColor& color ) { return 0.2126f * color._r + 0.7152f * color._g + 0.0722f * color._b; }

        /** @brief 사각형 안에서 오른쪽 · 아래 이웃과 휘도 차가 임계값을 넘는 픽셀 수입니다(글 · 테두리 · 모서리 모양). */
        static uint32 countEdges( const sw::AutomationImage& image, const AppUITestRect& rect )
        {
            uint32 count = 0;
            for ( int32 y = static_cast<int32>( rect._y ); y < static_cast<int32>( rect._y + rect._height ) - 1; ++y )
            {
                for ( int32 x = static_cast<int32>( rect._x ); x < static_cast<int32>( rect._x + rect._width ) - 1; ++x )
                {
                    const float32 luma  = computeLuma( readPixel( image, x, y ) );
                    const float32 right = computeLuma( readPixel( image, x + 1, y ) );
                    const float32 below = computeLuma( readPixel( image, x, y + 1 ) );
                    if ( sw::MathUtil::abs( luma - right ) + sw::MathUtil::abs( luma - below ) > kEdgeThreshold )
                        ++count;
                }
            }
            return count;
        }

        /** @brief 픽셀 사각형을 이미지 크기에 대한 비율(0..1) 영역으로 바꿉니다. */
        static sw::AutomationImageRegion toRegion( const sw::AutomationImage& image, const AppUITestRect& rect )
        {
            const float32 width  = static_cast<float32>( image._width );
            const float32 height = static_cast<float32>( image._height );
            return sw::AutomationImageRegion{ sw::MathUtil::clamp( rect._x / width, 0.0f, 1.0f ), sw::MathUtil::clamp( rect._y / height, 0.0f, 1.0f ),
                                              sw::MathUtil::clamp( ( rect._x + rect._width ) / width, 0.0f, 1.0f ),
                                              sw::MathUtil::clamp( ( rect._y + rect._height ) / height, 0.0f, 1.0f ) };
        }

        static sw::string describe( const AppUITestColor& color )
        {
            return "(" + sw::to_string( color._r ) + ", " + sw::to_string( color._g ) + ", " + sw::to_string( color._b ) + ")";
        }

        static const AppUITestRect* findRect( const AppUICapture& capture, const utf8* pName )
        {
            const auto iter = capture._mapRectByName.find( pName );
            return iter != capture._mapRectByName.end() ? &iter->second : nullptr;
        }

        /**
         * @brief 방금 돈 견본 시나리오의 산출물(스크린샷 · 레이아웃 덤프)을 @p outCapture 로 읽고 눈으로 볼 사본을 `uidemo<이름 덧붙임>_<백엔드>.*` 로 남깁니다.
         * @details 실패하면 단언을 기록하고 돌아온다(`_mapRectByName` 이 빈 채로 남는다).
         */
        static void loadCapture( const utf8* pSwitch, const utf8* pCopySuffix, AppUICapture& outCapture )
        {
            outCapture._backend         = pSwitch[0] == '-' ? sw::string( pSwitch + 1 ) : sw::string( "default" );
            const sw::string imagePath  = sw::FileUtil::joinPath( kOutputFolder, "uidemo.ppm" );
            const sw::string layoutPath = sw::FileUtil::joinPath( kOutputFolder, "uidemo.layout.txt" );
            sw::string       error;
            sw::string       layout;
            SW_ASSERT_TRUE_MSG( sw::AutomationImageMetric::loadPpm( imagePath, outCapture._image, error ), error.c_str() );
            SW_ASSERT_TRUE_MSG( sw::FileUtil::readTextFile( layoutPath, layout ), layoutPath.c_str() );
            SW_ASSERT_TRUE_MSG( parseDemoRects( layout, outCapture._image._width, outCapture._image._height, outCapture._mapRectByName ),
                                ( outCapture._backend + ": no demo screen in the layout dump" ).c_str() );
            // 다음 실행이 덮어쓰기 전에 이름을 붙여 남긴다(실패를 눈으로 볼 때).
            const sw::string  copyStem = sw::string( "uidemo" ) + pCopySuffix + "_" + outCapture._backend;
            sw::vector<uint8> bytes;
            if ( sw::FileUtil::readFile( imagePath, bytes ) )
                // 눈으로 볼 사본 — 실패는 writeFile 이 오류로 남긴다
                (void)sw::FileUtil::writeFile( sw::FileUtil::joinPath( kOutputFolder, copyStem + ".ppm" ), bytes.data(), bytes.size() );
            // 눈으로 볼 사본 — 실패는 writeTextFile 이 오류로 남긴다
            (void)sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( kOutputFolder, copyStem + ".layout.txt" ), layout );
        }

        /** @brief 견본의 알려진 영역을 단언합니다(백엔드 하나). */
        static void expectKnownRegions( const AppUICapture& capture )
        {
            const sw::string     label  = capture._backend + ": ";
            const AppUITestRect* pStart = findRect( capture, "Start" );
            const AppUITestRect* pRound = findRect( capture, "RoundBox" );
            const AppUITestRect* pClip  = findRect( capture, "ClipBox" );
            const AppUITestRect* pNine  = findRect( capture, "NineSlice" );
            SW_ASSERT_TRUE_MSG( pStart != nullptr && pRound != nullptr && pClip != nullptr && pNine != nullptr, ( label + "demo widgets missing from the layout dump" ).c_str() );

            // 주 단추(primary) 가운데 — 테마 강조색(0.25, 0.5, 1)이라 파랑이 빨강보다 뚜렷이 크다.
            const AppUITestRect  startCenter{ pStart->_x + pStart->_width * 0.4f, pStart->_y + pStart->_height * 0.3f, pStart->_width * 0.2f, pStart->_height * 0.4f };
            const AppUITestColor start = computeMean( capture._image, startCenter );
            SW_EXPECT_TRUE_MSG( start._b - start._r > 0.3f, ( label + "primary button center is not the accent color " + describe( start ) ).c_str() );

            // 둥근 상자 — 가운데는 초록, 왼쪽 위 모서리 바깥 픽셀은 왼쪽 패딩의 패널 바탕과 같다(둥근 모서리가 잘렸다).
            const AppUITestColor roundCenter = computeMean( capture._image, *pRound, static_cast<int32>( pRound->_height * 0.3f ) );
            const AppUITestColor corner      = readPixel( capture._image, static_cast<int32>( pRound->_x ) + 1, static_cast<int32>( pRound->_y ) + 1 );
            const AppUITestColor background  = readPixel( capture._image, static_cast<int32>( pRound->_x ) - 4, static_cast<int32>( pRound->_y + pRound->_height * 0.5f ) );
            SW_EXPECT_TRUE_MSG( roundCenter._g - roundCenter._r > 0.3f, ( label + "rounded box center is not green " + describe( roundCenter ) ).c_str() );
            SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( corner._g - background._g ) < 0.1f && sw::MathUtil::abs( corner._r - background._r ) < 0.1f,
                                ( label + "rounded corner " + describe( corner ) + " is not the panel background " + describe( background ) ).c_str() );

            // 자르기 — 상자 안은 빨강, 상자 오른쪽(다음 견본과의 틈)은 빨강이 아니다(자식은 상자 너비의 네 배다).
            const AppUITestColor clipInside = computeMean( capture._image, *pClip, 2 );
            const float32        gapLeft    = pClip->_x + pClip->_width + 2.0f;
            const float32        gapWidth   = pNine->_x - 2.0f - gapLeft;
            SW_ASSERT_TRUE_MSG( gapWidth >= 1.0f, ( label + "no gap between the clip box and the nine-slice sample" ).c_str() );
            const AppUITestColor clipOutside =
                computeMean( capture._image, AppUITestRect{ gapLeft, pClip->_y + pClip->_height * 0.25f, gapWidth, pClip->_height * 0.5f } );
            SW_EXPECT_TRUE_MSG( clipInside._r - clipInside._g > 0.5f, ( label + "clip box content is not red " + describe( clipInside ) ).c_str() );
            SW_EXPECT_TRUE_MSG( clipOutside._r - clipOutside._g < 0.2f, ( label + "clipped child leaks past the clip box " + describe( clipOutside ) ).c_str() );
        }

        /** @brief @p capture 를 기준 @p reference(첫 백엔드)와 영역마다 비교합니다. */
        static void expectMatchesReference( const AppUICapture& reference, const AppUICapture& capture )
        {
            const sw::string label = capture._backend + " vs " + reference._backend + ": ";
            SW_ASSERT_TRUE_MSG( capture._image._width == reference._image._width && capture._image._height == reference._image._height,
                                ( label + "screenshot sizes differ" ).c_str() );
            for ( const utf8* pName : kArrRegionName )
            {
                const AppUITestRect* pRect        = findRect( reference, pName );
                const AppUITestRect* pCaptureRect = findRect( capture, pName );
                SW_ASSERT_TRUE_MSG( pRect != nullptr && pCaptureRect != nullptr, ( label + pName + " missing" ).c_str() );
                SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( pRect->_x - pCaptureRect->_x ) < 0.5f && sw::MathUtil::abs( pRect->_width - pCaptureRect->_width ) < 0.5f,
                                    ( label + pName + " layout differs" ).c_str() );
                const sw::AutomationImageRegion region = toRegion( reference._image, *pRect );
                float64                         differingFraction{ 0.0 };
                sw::string                      error;
                SW_EXPECT_TRUE_MSG( sw::AutomationImageMetric::measure( capture._image, region, sw::AutomationImageMetricKind::DifferingFraction, 0.0f, &reference._image,
                                                                        differingFraction, error ),
                                    ( label + pName + ": " + error ).c_str() );
                SW_LOG_INFO( "AppUITest: %#%# differing fraction %#", label, pName, differingFraction );
                SW_EXPECT_TRUE_MSG( differingFraction <= kMaxDifferingFraction, ( label + pName + " differing fraction " + sw::to_string( differingFraction ) ).c_str() );
                const uint32  expectedEdges = countEdges( reference._image, *pRect );
                const uint32  actualEdges   = countEdges( capture._image, *pRect );
                const float32 edgeDifference =
                    sw::MathUtil::abs( static_cast<float32>( actualEdges ) - static_cast<float32>( expectedEdges ) ) / sw::MathUtil::max( 1.0f, static_cast<float32>( expectedEdges ) );
                SW_EXPECT_TRUE_MSG( edgeDifference <= kEdgeTolerance, ( label + pName + " edges " + sw::to_string( actualEdges ) + " vs " +
                                                                        sw::to_string( expectedEdges ) )
                                                                          .c_str() );
            }
        }
    };
} // namespace

/**
 * @brief [AppUITest] UI 견본 화면이 모든 백엔드에서 같은 모양이다 — 알려진 영역(강조색 단추 · 둥근 모서리 바깥 · 자르기 밖)과 백엔드 사이 영역의 다른 화소 비율 · 가장자리 수
 * @details 산출물은 백엔드마다 `Bin/Saved/Automation/engine.uidemo/uidemo_<백엔드>.ppm` · `.layout.txt` 로 옮겨 둔다(실패를 눈으로 볼 때).
 */
SW_TEST_CASE( AppUITest, DemoScreenMatchesAcrossBackends )
{
    using Internal = AppUITestInternal;
#if defined( SW_SHIPPING )
    constexpr const utf8* kArrBackendSwitch[] = { "" };
#else
    #define SW_APP_UI_BACKEND_SWITCH( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) "-" CommandLineName,
    constexpr const utf8* kArrBackendSwitch[] = { SW_RHI_BACKEND_TABLE( SW_APP_UI_BACKEND_SWITCH ) };
    #undef SW_APP_UI_BACKEND_SWITCH
#endif
    sw::vector<AppUICapture> listCapture;
    bool                     bAnyRan = false;
    for ( const utf8* pSwitch : kArrBackendSwitch )
    {
        sw::string                scenarioLines;
        const test::AppRunOutcome outcome = test::AppTestUtil::runScenario( Internal::kScenarioPath, pSwitch, scenarioLines );
        bAnyRan                           = bAnyRan || test::hasRunHere( outcome );
        if ( outcome != test::AppRunOutcome::Passed )
            continue; // 실패는 runScenario 가 기록했다
        AppUICapture capture{};
        Internal::loadCapture( pSwitch, "", capture );
        Internal::expectKnownRegions( capture );
        listCapture.push_back( std::move( capture ) );
    }
    if ( bAnyRan == false )
        SW_TEST_SKIP( "no backend could run the UI demo scenario on this machine" );
    for ( size_t index = 1; index < listCapture.size(); ++index )
    {
        Internal::expectMatchesReference( listCapture[0], listCapture[index] );
    }
}

/**
 * @brief [AppUITest] 글리프 아틀라스를 MSDF 로 켜(`-gv_fontMsdfGlyphs=1`) 견본 화면을 그려도 SDF 와 같은 모양이다 — 알려진 영역 단언과 패널의 평균 휘도 · 가장자리 수가 SDF 와 가깝다
 * @details MsdfGlyphs 계획 M5. 글자가 안 그려지거나(R 채널만 읽는 틀린 경로) 모서리가 크게 뭉개지면 휘도 · 가장자리 수가 SDF 와 벌어진다. 큰 글자 모서리가 날카로운지는
 *          눈으로 본다(`uidemo_msdf_<백엔드>.ppm` 을 `uidemo_<백엔드>.ppm` 과 견준다). 백엔드 하나(처음 돌아간 것)만 본다.
 */
SW_TEST_CASE( AppUITest, MsdfGlyphsMatchSdfDemoScreen )
{
    using Internal = AppUITestInternal;
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "gv_fontMsdfGlyphs is a Release-only test switch" );
#else
    #define SW_APP_UI_BACKEND_SWITCH( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) "-" CommandLineName,
    constexpr const utf8* kArrBackendSwitch[] = { SW_RHI_BACKEND_TABLE( SW_APP_UI_BACKEND_SWITCH ) };
    #undef SW_APP_UI_BACKEND_SWITCH
    for ( const utf8* pSwitch : kArrBackendSwitch )
    {
        sw::string                sdfLines;
        const test::AppRunOutcome sdfOutcome = test::AppTestUtil::runScenario( Internal::kScenarioPath, pSwitch, sdfLines );
        if ( sdfOutcome != test::AppRunOutcome::Passed )
            continue; // 이 백엔드는 이 머신에서 못 돈다 — 실패는 runScenario 가 기록했다
        AppUICapture sdf{};
        Internal::loadCapture( pSwitch, "", sdf );
        sw::string                msdfLines;
        const test::AppRunOutcome msdfOutcome = test::AppTestUtil::runScenario( Internal::kScenarioPath, pSwitch, msdfLines, "-gv_fontMsdfGlyphs=1" );
        SW_ASSERT_TRUE_MSG( msdfOutcome == test::AppRunOutcome::Passed, "the UI demo scenario failed with MSDF glyphs" );
        AppUICapture msdf{};
        Internal::loadCapture( pSwitch, "_msdf", msdf );
        Internal::expectKnownRegions( msdf );
        const AppUITestRect* pSdfPanel  = Internal::findRect( sdf, "DemoPanel" );
        const AppUITestRect* pMsdfPanel = Internal::findRect( msdf, "DemoPanel" );
        SW_ASSERT_TRUE_MSG( pSdfPanel != nullptr && pMsdfPanel != nullptr, "DemoPanel missing from the layout dump" );
        const float32 sdfLuma  = Internal::computeLuma( Internal::computeMean( sdf._image, *pSdfPanel ) );
        const float32 msdfLuma = Internal::computeLuma( Internal::computeMean( msdf._image, *pMsdfPanel ) );
        SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( msdfLuma - sdfLuma ) <= Internal::kMsdfLumaTolerance * sw::MathUtil::max( sdfLuma, 0.01f ),
                            ( "panel mean luma SDF " + sw::to_string( sdfLuma ) + " vs MSDF " + sw::to_string( msdfLuma ) ).c_str() );
        const uint32  sdfEdges  = Internal::countEdges( sdf._image, *pSdfPanel );
        const uint32  msdfEdges = Internal::countEdges( msdf._image, *pMsdfPanel );
        const float32 edgeDifference =
            sw::MathUtil::abs( static_cast<float32>( msdfEdges ) - static_cast<float32>( sdfEdges ) ) / sw::MathUtil::max( 1.0f, static_cast<float32>( sdfEdges ) );
        SW_EXPECT_TRUE_MSG( edgeDifference <= Internal::kMsdfEdgeTolerance, ( "panel edges SDF " + sw::to_string( sdfEdges ) + " vs MSDF " + sw::to_string( msdfEdges ) ).c_str() );
        return;
    }
    SW_TEST_SKIP( "no backend could run the UI demo scenario on this machine" );
#endif
}
