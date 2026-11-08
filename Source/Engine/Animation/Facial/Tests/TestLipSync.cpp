#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Facial/LipSync.h"

using namespace sw;

namespace
{
    struct LipSyncTestUtil
    {
        static constexpr uint32 kSampleRate = 16000;
        // 비즘 번호(`makeSettings` 의 순서).
        static constexpr uint32 kSilence = 0;
        static constexpr uint32 kVowel   = 1;
        static constexpr uint32 kPlosive = 2;
        static constexpr uint32 kHiss    = 3;

        [[nodiscard]] static LipSyncViseme makeViseme( string_view name, const float3& bandShape, const float2& formantHz, LipSyncConsonant consonant )
        {
            LipSyncViseme viseme;
            viseme._name      = hashed_string( name );
            viseme._bandShape = bandShape;
            viseme._formantHz = formantHz;
            viseme._consonant = consonant;
            return viseme;
        }

        [[nodiscard]] static LipSyncSettings makeSettings()
        {
            LipSyncSettings settings;
            settings._listViseme.push_back( makeViseme( "sil", float3{ 0.0f, 0.0f, 0.0f }, float2{ 0.0f, 0.0f }, LipSyncConsonant::None ) );
            settings._listViseme.push_back( makeViseme( "aa", float3{ 0.6f, 0.3f, 0.1f }, float2{ 700.0f, 1200.0f }, LipSyncConsonant::None ) );
            settings._listViseme.push_back( makeViseme( "pp", float3{ 0.3f, 0.3f, 0.3f }, float2{ 0.0f, 0.0f }, LipSyncConsonant::Plosive ) );
            settings._listViseme.push_back( makeViseme( "ss", float3{ 0.0f, 0.1f, 0.9f }, float2{ 0.0f, 0.0f }, LipSyncConsonant::Fricative ) );
            settings._fallbackViseme = hashed_string( "aa" );
            return settings;
        }

        static void appendSine( vector<float32>& inoutListSample, float32 hz, float32 amplitude, size_t count, uint32 sampleRate = kSampleRate )
        {
            const size_t begin = inoutListSample.size();
            for ( size_t index = 0; index < count; ++index )
            {
                inoutListSample.push_back( amplitude * MathUtil::sin( 2.0f * MathUtil::kPi * hz * static_cast<float32>( begin + index ) / static_cast<float32>( sampleRate ) ) );
            }
        }

        /** @brief 정해진 씨앗의 잡음입니다(실행마다 같다). */
        static void appendNoise( vector<float32>& inoutListSample, float32 amplitude, size_t count )
        {
            uint32 state = 12345u;
            for ( size_t index = 0; index < count; ++index )
            {
                state = state * 1664525u + 1013904223u;
                inoutListSample.push_back( amplitude * ( static_cast<float32>( state >> 8 ) / 8388608.0f - 1.0f ) );
            }
        }

        /** @brief 임펄스 열을 두 공명기에 차례로 통과시킨 합성 모음입니다 — 답을 아는 포먼트 측정 입력입니다. */
        [[nodiscard]] static vector<float32> makeVowel( uint32 sampleRate, float32 firstHz, float32 secondHz, size_t count )
        {
            vector<float32> listSample( count, 0.0f );
            const size_t    period = sampleRate / 120u;
            for ( size_t index = 0; index < count; index += period )
            {
                listSample[index] = 1.0f;
            }
            for ( const float32 hz : { firstHz, secondHz } )
            {
                const float64 radius = MathUtil::exp( -MathUtil::kPi64 * 80.0 / static_cast<float64>( sampleRate ) );
                const float64 a1     = 2.0 * radius * MathUtil::cos( 2.0 * MathUtil::kPi64 * static_cast<float64>( hz ) / static_cast<float64>( sampleRate ) );
                const float64 a2     = -radius * radius;
                float64       y1     = 0.0;
                float64       y2     = 0.0;
                for ( float32& sample : listSample )
                {
                    const float64 output = static_cast<float64>( sample ) + a1 * y1 + a2 * y2;
                    y2                   = y1;
                    y1                   = output;
                    sample               = static_cast<float32>( output );
                }
            }
            return listSample;
        }

        [[nodiscard]] static float32 readWeight( const VisemeTrack& track, uint32 frame, uint32 viseme ) { return track._listWeight[static_cast<size_t>( frame ) * track.getVisemeCount() + viseme]; }

        [[nodiscard]] static VisemeTrack makeTrack()
        {
            VisemeTrack track;
            track._frameRate  = 10.0f;
            track._listViseme = { hashed_string( "sil" ), hashed_string( "aa" ) };
            track._listWeight = { 1.0f, 0.0f, 0.0f, 1.0f };
            return track;
        }
    };
} // namespace

SW_TEST_CASE( LipSyncTest, SettingsValidateAcceptsAWellFormedTable )
{
    SW_EXPECT_TRUE( LipSyncTestUtil::makeSettings().validate( "test" ) );
    SW_EXPECT_TRUE( LipSyncTestUtil::makeSettings().findVisemeIndex( hashed_string( "pp" ) ) == 2u );
    SW_EXPECT_FALSE( LipSyncTestUtil::makeSettings().findVisemeIndex( hashed_string( "nope" ) ).has_value() );
}

SW_TEST_CASE( LipSyncTest, SettingsValidateRefusesEachBrokenField )
{
    {
        SW_EXPECT_LOG( "LipSync.LipSyncSettingsFrameRate" );
        LipSyncSettings settings = LipSyncTestUtil::makeSettings();
        settings._frameRate      = 0.0f;
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
    {
        LipSyncSettings settings = LipSyncTestUtil::makeSettings(); // 같은 오류 id - 위의 기대가 함께 받는다
        settings._fullRms        = settings._silenceRms;
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
    {
        SW_EXPECT_LOG( "LipSync.LipSyncSettingsMust" );
        LipSyncSettings settings = LipSyncTestUtil::makeSettings();
        settings._midBandHz      = float2{ 1600.0f, 600.0f };
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
    {
        SW_EXPECT_LOG( "LipSync.LipSyncSettingsListViseme" );
        LipSyncSettings settings = LipSyncTestUtil::makeSettings();
        settings._listViseme.resize( 1 );
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
    {
        SW_EXPECT_LOG( "LipSync.LipSyncSettingsViseme" ); // 이름이 겹친다
        LipSyncSettings settings      = LipSyncTestUtil::makeSettings();
        settings._listViseme[2]._name = hashed_string( "aa" );
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
    {
        SW_EXPECT_LOG( "LipSync.LipSyncSettingsViseme2" ); // F1 > F2
        LipSyncSettings settings           = LipSyncTestUtil::makeSettings();
        settings._listViseme[1]._formantHz = float2{ 1200.0f, 700.0f };
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
    {
        SW_EXPECT_LOG( "LipSync.LipSyncSettingsViseme3" ); // 모음에 자음 단서
        LipSyncSettings settings           = LipSyncTestUtil::makeSettings();
        settings._listViseme[1]._consonant = LipSyncConsonant::Plosive;
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
    {
        SW_EXPECT_LOG( "LipSync.LipSyncSettingsPlosiveOnsetRatio" );
        LipSyncSettings settings    = LipSyncTestUtil::makeSettings();
        settings._plosiveOnsetRatio = 1.0f;
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
    {
        SW_EXPECT_LOG( "LipSync.LipSyncSettingsFallbackViseme" );
        LipSyncSettings settings = LipSyncTestUtil::makeSettings();
        settings._fallbackViseme = hashed_string( "nope" );
        SW_EXPECT_FALSE( settings.validate( "test" ) );
    }
}

SW_TEST_CASE( LipSyncTest, TrackSampleInterpolatesAndEndsAtZero )
{
    const VisemeTrack track = LipSyncTestUtil::makeTrack();
    SW_EXPECT_EQUAL( track.getFrameCount(), 2u );
    SW_EXPECT_NEAR( track.getDuration(), 0.2f, 1e-5f );

    vector<float32> listWeight;
    track.sample( 0.05f, listWeight ); // 두 프레임의 한가운데
    SW_REQUIRE_EQUAL( listWeight.size(), size_t{ 2 } );
    SW_EXPECT_NEAR( listWeight[0], 0.5f, 1e-5f );
    SW_EXPECT_NEAR( listWeight[1], 0.5f, 1e-5f );

    track.sample( 0.15f, listWeight ); // 마지막 프레임은 다음이 없어 그대로다
    SW_EXPECT_NEAR( listWeight[1], 1.0f, 1e-5f );

    track.sample( 0.25f, listWeight ); // 끝을 지났다
    SW_EXPECT_EQUAL( listWeight[1], 0.0f );
    track.sample( -1.0f, listWeight );
    SW_EXPECT_EQUAL( listWeight[0], 0.0f );
}

SW_TEST_CASE( LipSyncTest, TrackRoundTripsThroughXMLAndAFile )
{
    const VisemeTrack track = LipSyncTestUtil::makeTrack();
    VisemeTrack       parsed;
    SW_REQUIRE_TRUE( parsed.loadFromXMLText( track.toXML(), "test" ) );
    SW_EXPECT_EQUAL( parsed._frameRate, track._frameRate );
    SW_EXPECT_TRUE( parsed._listViseme == track._listViseme );
    SW_EXPECT_TRUE( parsed._listWeight == track._listWeight );

    const optional<AbsolutePath> path = AbsolutePath::tryMake( test::makeScratchPath( "line.visemes.xml" ) );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_REQUIRE_TRUE( track.saveToFile( *path ) );
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path->str(), text ) );
    SW_EXPECT_TRUE( parsed.loadFromXMLText( text, "test" ) );
    SW_EXPECT_EQUAL( parsed.getFrameCount(), 2u );
}

// 틀린 트랙은 오류를 남기고 비어 돌아온다 - 반쯤 읽은 내용으로 입이 움직이면 안 된다.
SW_TEST_CASE( LipSyncTest, BrokenTrackIsAnErrorAndLeavesNothing )
{
    VisemeTrack track = LipSyncTestUtil::makeTrack();
    {
        SW_EXPECT_LOG( "LipSync.VisemeTrackFrameRate" );
        SW_EXPECT_FALSE( track.loadFromXMLText( "<VisemeTrack _frameRate=\"0\"><_listViseme><item>sil</item></_listViseme><_listFrame><Frame>1</Frame></_listFrame></VisemeTrack>", "test" ) );
        SW_EXPECT_EQUAL( track.getVisemeCount(), 0u );
        SW_EXPECT_EQUAL( track.getFrameCount(), 0u );
    }
    {
        SW_EXPECT_LOG( "LipSync.VisemeTrackNeedsFrameRate" );
        SW_EXPECT_FALSE( track.loadFromXMLText( "<VisemeTrack _frameRate=\"30\"><_listViseme></_listViseme><_listFrame></_listFrame></VisemeTrack>", "test" ) );
    }
    {
        SW_EXPECT_LOG( "LipSync.VisemeTrackFrameMust" );
        SW_EXPECT_FALSE( track.loadFromXMLText( "<VisemeTrack _frameRate=\"30\"><_listViseme><item>sil</item><item>aa</item></_listViseme><_listFrame><Frame>1</Frame></_listFrame></VisemeTrack>", "test" ) );
        SW_EXPECT_EQUAL( track.getVisemeCount(), 0u );
    }
}

SW_TEST_CASE( LipSyncTest, TrackPathReplacesTheAudioExtension )
{
    SW_EXPECT_EQUAL( VisemeTrack::makePathForAudio( "a/line.wav" ), string( "a/line.visemes.xml" ) );
}

SW_TEST_CASE( LipSyncTest, OpennessMapsRmsBetweenSilenceAndFull )
{
    const LipSyncSettings settings = LipSyncTestUtil::makeSettings();
    SW_EXPECT_EQUAL( VisemeTrack::computeOpenness( 0.0f, settings ), 0.0f );
    SW_EXPECT_EQUAL( VisemeTrack::computeOpenness( 1.0f, settings ), 1.0f );
    SW_EXPECT_NEAR( VisemeTrack::computeOpenness( ( settings._silenceRms + settings._fullRms ) * 0.5f, settings ), 0.5f, 1e-5f );
}

SW_TEST_CASE( LipSyncTest, RmsReadsTheWindowAroundTheTime )
{
    vector<float32> listSample( 1600, 0.0f );
    for ( size_t index = 800; index < listSample.size(); ++index )
    {
        listSample[index] = 0.5f;
    }
    SW_EXPECT_EQUAL( VisemeTrack::computeRms( listSample, 16000, 0.02f, 0.01f ), 0.0f );
    SW_EXPECT_NEAR( VisemeTrack::computeRms( listSample, 16000, 0.08f, 0.01f ), 0.5f, 1e-5f );
    SW_EXPECT_EQUAL( VisemeTrack::computeRms( listSample, 16000, 1.0f, 0.01f ), 0.0f ); // 끝 뒤
    SW_EXPECT_EQUAL( VisemeTrack::computeRms( listSample, 16000, -1.0f, 0.01f ), 0.0f );
    SW_EXPECT_EQUAL( VisemeTrack::computeRms( listSample, 0, 0.08f, 0.01f ), 0.0f );
    SW_EXPECT_EQUAL( VisemeTrack::computeRms( {}, 16000, 0.08f, 0.01f ), 0.0f );
}

SW_TEST_CASE( LipSyncTest, FormantsOfASyntheticVowelAreFound )
{
    float2                formantHz;
    const vector<float32> listSample = LipSyncTestUtil::makeVowel( LipSyncTestUtil::kSampleRate, 700.0f, 1800.0f, 1024 );
    SW_REQUIRE_TRUE( VisemeTrack::tryComputeFormants( listSample.data(), listSample.size(), LipSyncTestUtil::kSampleRate, formantHz ) );
    SW_EXPECT_NEAR( formantHz._x, 700.0f, 100.0f );
    SW_EXPECT_NEAR( formantHz._y, 1800.0f, 150.0f );

    // 높은 표본율은 솎아서 같은 답을 낸다.
    const vector<float32> listHighRate = LipSyncTestUtil::makeVowel( 48000, 700.0f, 1800.0f, 3072 );
    SW_REQUIRE_TRUE( VisemeTrack::tryComputeFormants( listHighRate.data(), listHighRate.size(), 48000, formantHz ) );
    SW_EXPECT_NEAR( formantHz._x, 700.0f, 100.0f );
    SW_EXPECT_NEAR( formantHz._y, 1800.0f, 150.0f );
}

SW_TEST_CASE( LipSyncTest, FormantsNeedEnoughSamples )
{
    float2                formantHz;
    const vector<float32> listSample( 16, 0.5f );
    SW_EXPECT_FALSE( VisemeTrack::tryComputeFormants( listSample.data(), listSample.size(), LipSyncTestUtil::kSampleRate, formantHz ) ); // 차수보다 짧다
    SW_EXPECT_FALSE( VisemeTrack::tryComputeFormants( nullptr, 1024, LipSyncTestUtil::kSampleRate, formantHz ) );
    SW_EXPECT_FALSE( VisemeTrack::tryComputeFormants( listSample.data(), listSample.size(), 0, formantHz ) );
    const vector<float32> listSilence( 1024, 0.0f );
    SW_EXPECT_FALSE( VisemeTrack::tryComputeFormants( listSilence.data(), listSilence.size(), LipSyncTestUtil::kSampleRate, formantHz ) ); // 자기상관이 0 이다
}

SW_TEST_CASE( LipSyncTest, AnalyzeWithNothingKeepsTheNamesAndNoFrames )
{
    const LipSyncSettings settings = LipSyncTestUtil::makeSettings();
    VisemeTrack           track;
    VisemeTrack::analyze( {}, LipSyncTestUtil::kSampleRate, settings, track );
    SW_EXPECT_EQUAL( track.getVisemeCount(), 4u );
    SW_EXPECT_EQUAL( track.getFrameCount(), 0u );
    VisemeTrack::analyze( vector<float32>( 100, 0.5f ), 0, settings, track );
    SW_EXPECT_EQUAL( track.getFrameCount(), 0u );
}

SW_TEST_CASE( LipSyncTest, AnalyzeCountsPartialLastFrame )
{
    const LipSyncSettings settings = LipSyncTestUtil::makeSettings();
    VisemeTrack           track;
    VisemeTrack::analyze( vector<float32>( 534, 0.0f ), LipSyncTestUtil::kSampleRate, settings, track ); // 프레임당 533 표본
    SW_EXPECT_EQUAL( track.getFrameCount(), 2u );
    SW_EXPECT_EQUAL( LipSyncTestUtil::readWeight( track, 1, LipSyncTestUtil::kSilence ), 1.0f );
}

SW_TEST_CASE( LipSyncTest, AnalyzeSilenceIsAllSilenceViseme )
{
    const LipSyncSettings settings = LipSyncTestUtil::makeSettings();
    VisemeTrack           track;
    VisemeTrack::analyze( vector<float32>( 2000, 0.0f ), LipSyncTestUtil::kSampleRate, settings, track );
    SW_REQUIRE_TRUE( track.getFrameCount() > 0 );
    for ( uint32 frame = 0; frame < track.getFrameCount(); ++frame )
    {
        SW_EXPECT_EQUAL( LipSyncTestUtil::readWeight( track, frame, LipSyncTestUtil::kSilence ), 1.0f );
        SW_EXPECT_EQUAL( LipSyncTestUtil::readWeight( track, frame, LipSyncTestUtil::kVowel ), 0.0f );
    }
}

// 유성음(낮은 사인)은 무음 뒤에 크게 올라도 영교차가 드물어 파열음이 아니라 모음이다.
SW_TEST_CASE( LipSyncTest, AnalyzeLowToneIsAVowelNotAPlosive )
{
    const LipSyncSettings settings = LipSyncTestUtil::makeSettings();
    vector<float32>       listSample;
    LipSyncTestUtil::appendSine( listSample, 300.0f, 0.5f, 3000 );
    VisemeTrack track;
    VisemeTrack::analyze( listSample, LipSyncTestUtil::kSampleRate, settings, track );
    SW_REQUIRE_TRUE( track.getFrameCount() >= 3 );
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        SW_EXPECT_TRUE( LipSyncTestUtil::readWeight( track, frame, LipSyncTestUtil::kVowel ) > LipSyncTestUtil::readWeight( track, frame, LipSyncTestUtil::kPlosive ) );
        SW_EXPECT_TRUE( LipSyncTestUtil::readWeight( track, frame, LipSyncTestUtil::kVowel ) > LipSyncTestUtil::readWeight( track, frame, LipSyncTestUtil::kHiss ) );
    }
}

// 잡음이 무음 뒤에 터지면 그 첫 프레임만 파열음이고, 이어지는 잡음은 마찰음이다.
SW_TEST_CASE( LipSyncTest, AnalyzeNoiseBurstIsAPlosiveThenAFricative )
{
    const LipSyncSettings settings = LipSyncTestUtil::makeSettings();
    vector<float32>       listSample;
    LipSyncTestUtil::appendNoise( listSample, 0.6f, 533 * 4 );
    VisemeTrack track;
    VisemeTrack::analyze( listSample, LipSyncTestUtil::kSampleRate, settings, track );
    SW_REQUIRE_TRUE( track.getFrameCount() >= 3 );
    SW_EXPECT_TRUE( LipSyncTestUtil::readWeight( track, 0, LipSyncTestUtil::kPlosive ) > LipSyncTestUtil::readWeight( track, 0, LipSyncTestUtil::kHiss ) );
    SW_EXPECT_TRUE( LipSyncTestUtil::readWeight( track, 2, LipSyncTestUtil::kHiss ) > LipSyncTestUtil::readWeight( track, 2, LipSyncTestUtil::kPlosive ) );
}

// 프레임의 가중치는 언제나 합이 1 이고, 무음 비즘이 닫힌 만큼을 갖는다.
SW_TEST_CASE( LipSyncTest, AnalyzeWeightsSumToOneAndFollowOpenness )
{
    const LipSyncSettings settings = LipSyncTestUtil::makeSettings();
    vector<float32>       listSample;
    LipSyncTestUtil::appendSine( listSample, 300.0f, 0.191f, 5000 ); // RMS 0.135 - 무음과 최대의 한가운데
    VisemeTrack track;
    VisemeTrack::analyze( listSample, LipSyncTestUtil::kSampleRate, settings, track );
    SW_REQUIRE_TRUE( track.getFrameCount() >= 3 );
    for ( uint32 frame = 0; frame < track.getFrameCount(); ++frame )
    {
        float32 sum = 0.0f;
        for ( uint32 viseme = 0; viseme < track.getVisemeCount(); ++viseme )
        {
            sum += LipSyncTestUtil::readWeight( track, frame, viseme );
        }
        SW_EXPECT_NEAR( sum, 1.0f, 1e-4f );
    }
    SW_EXPECT_NEAR( LipSyncTestUtil::readWeight( track, 2, LipSyncTestUtil::kSilence ), 0.5f, 0.05f );
}
