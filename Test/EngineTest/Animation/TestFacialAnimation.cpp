#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Transport/UDPNetTransport.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Facial/FacialLiveSource.h"
#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Facial/LiveLinkFace.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/LipSyncImport.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/FacialAnimationComponent.h"
#include "Engine/Object/Component/3D/LiveLinkFaceComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/AssetArrivalTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// FacialAnimationTest — 모프 타깃 임포트 결과 · 립싱크 분석 · 얼굴 리그(표정 · 비즘 · 깜빡임 · 시선)가 유닛의 모프 가중치 · 눈 본에 닿는 것.

namespace
{
    /** @brief 표정 채널 하나(`Happy`)만 내는 실시간 입력입니다. */
    class TestFacialLiveSource final : public IFacialLiveSource
    {
    public:
        [[nodiscard]] bool tryGetChannel( const hashed_string& channel, float32& outWeight ) const override
        {
            if ( channel != hashed_string( "Happy" ) )
                return false;
            outWeight = _happy;
            return true;
        }

        float32 _happy{ 0.0f };
    };

    struct TestFacialAnimationInternal
    {
        static constexpr string_view kMeshPath           = "game/empty/models/testhead.mesh";
        static constexpr string_view kSkeletonPath       = "game/empty/models/testhead/testhead.skeleton.json";
        static constexpr string_view kRigPath            = "game/empty/models/testhead.facial.json";
        static constexpr string_view kClipFolder         = "game/empty/models/testhead/clips";
        static constexpr uint32      kSampleRate         = 16000;
        static constexpr string_view kLiveLinkLayoutPath = "engine/animation/livelinkface.json";

        /** @brief 테스트 머리(유닛) + 얼굴 컴포넌트를 만들고 시작합니다. 애니메이터는 @p pAnimatorState 가 있을 때만 붙입니다. */
        static FacialAnimationComponent* createHead( GameObjectManager& manager, SkeletalMeshComponent*& outUnit, const utf8* pAnimatorState = nullptr,
                                                     string_view clipFolder = kClipFolder )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Head" ) );
            if ( pObject == nullptr )
                return nullptr;
            outUnit = pObject->addComponent<SkeletalMeshComponent>();
            if ( outUnit == nullptr )
                return nullptr;
            outUnit->setSkeletonPath( kSkeletonPath );
            outUnit->setMeshID( kMeshPath );
            outUnit->resolveRenderAssets();
            outUnit->dispatchBeginPlay();
            if ( pAnimatorState != nullptr )
            {
                SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
                if ( pAnimator == nullptr )
                    return nullptr;
                pAnimator->setClipFolder( clipFolder );
                pAnimator->setInitialState( pAnimatorState );
                pAnimator->dispatchBeginPlay();
            }
            // 메시 · 클립은 워커가 읽는다 — 표정이 모프 타깃을 찾기 전에 도착을 마친다.
            if ( ::test::finishAssetArrivals( manager ) == false )
                return nullptr;
            FacialAnimationComponent* pFace = pObject->addComponent<FacialAnimationComponent>();
            if ( pFace == nullptr )
                return nullptr;
            pFace->setFacialRigPath( kRigPath );
            pFace->setAutoBlink( false );
            pFace->setSaccades( false );
            pFace->dispatchBeginPlay();
            return pFace;
        }

        /** @brief 이름의 모프 가중치입니다(없으면 -1). */
        static float32 getWeight( const SkeletalMeshComponent& unit, const utf8* pTargetName )
        {
            const int32 targetIndex = unit.findMorphTargetIndex( hashed_string( pTargetName ) );
            if ( targetIndex < 0 || static_cast<size_t>( targetIndex ) >= unit.getMorphWeights().size() )
                return -1.0f;
            return unit.getMorphWeights()[static_cast<size_t>( targetIndex )];
        }

        /**
         * @brief 두 포먼트 봉우리로 모양을 낸 120 Hz 목소리(배음 합)입니다 — 모음 하나를 흉내 냅니다.
         * @details 배음 크기는 실제 목소리처럼 옥타브마다 -6 dB(1/f) 기운다(성문 -12 dB + 입술 방사 +6 dB). 분석의 프리엠퍼시스가 이 기울기를 편다고 보므로,
         *          평평한 원천이면 낮은 포먼트가 위쪽 배음으로 끌린다(EE 300 Hz → 370 Hz).
         */
        static void appendVowel( vector<float32>& inoutListSample, float32 firstFormant, float32 secondFormant, float32 seconds )
        {
            auto resonance = []( float32 frequency, float32 centre, float32 width )
            {
                const float32 offset = ( frequency - centre ) / width;
                return 1.0f / ( 1.0f + offset * offset );
            };
            constexpr float32 kPitchHz = 120.0f;
            vector<float32>   listHarmonicFrequency;
            vector<float32>   listHarmonicAmplitude;
            float32           amplitudeSum = 0.0f;
            for ( float32 frequency = kPitchHz; frequency < 7000.0f; frequency += kPitchHz )
            {
                const float32 sourceTilt = kPitchHz / frequency;
                const float32 amplitude  = sourceTilt * ( resonance( frequency, firstFormant, 120.0f ) + 0.7f * resonance( frequency, secondFormant, 180.0f ) );
                listHarmonicFrequency.push_back( frequency );
                listHarmonicAmplitude.push_back( amplitude );
                amplitudeSum += amplitude;
            }
            const uint32 count = static_cast<uint32>( seconds * static_cast<float32>( kSampleRate ) );
            const size_t start = inoutListSample.size();
            for ( uint32 sampleIndex = 0; sampleIndex < count; ++sampleIndex )
            {
                const float32 time  = static_cast<float32>( start + sampleIndex ) / static_cast<float32>( kSampleRate );
                float32       value = 0.0f;
                for ( size_t harmonic = 0; harmonic < listHarmonicFrequency.size(); ++harmonic )
                {
                    value += listHarmonicAmplitude[harmonic] * MathUtil::sin( 2.0f * MathUtil::kPi * listHarmonicFrequency[harmonic] * time );
                }
                inoutListSample.push_back( 0.5f * value / amplitudeSum );
            }
        }

        /** @brief 높은 대역을 강조한 잡음(두 번 차분한 백색 잡음 — 치찰음)입니다. */
        static void appendHiss( vector<float32>& inoutListSample, float32 seconds )
        {
            uint32       state = 12345u;
            float32      arrHistory[2]{ 0.0f, 0.0f };
            const uint32 count = static_cast<uint32>( seconds * static_cast<float32>( kSampleRate ) );
            for ( uint32 sampleIndex = 0; sampleIndex < count; ++sampleIndex )
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                const float32 noise = static_cast<float32>( state & 0xFFFFu ) / 32767.5f - 1.0f;
                inoutListSample.push_back( 0.5f * ( noise - 2.0f * arrHistory[0] + arrHistory[1] ) );
                arrHistory[1] = arrHistory[0];
                arrHistory[0] = noise;
            }
        }

        /** @brief 한 극 낮은 통과(@p cutoffHz)를 거친 백색 잡음입니다 — 높은 대역이 주가 아닌 마찰음(f · th)을 흉내 냅니다. 영교차는 유성음보다 잦다. */
        static void appendLowpassNoise( vector<float32>& inoutListSample, float32 cutoffHz, float32 seconds )
        {
            uint32        state  = 67890u;
            float32       output = 0.0f;
            const float32 alpha  = 1.0f - MathUtil::exp( -2.0f * MathUtil::kPi * cutoffHz / static_cast<float32>( kSampleRate ) );
            const uint32  count  = static_cast<uint32>( seconds * static_cast<float32>( kSampleRate ) );
            for ( uint32 sampleIndex = 0; sampleIndex < count; ++sampleIndex )
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                const float32 noise = static_cast<float32>( state & 0xFFFFu ) / 32767.5f - 1.0f;
                output += alpha * ( noise - output );
                inoutListSample.push_back( 1.5f * output );
            }
        }

        /**
         * @brief 파열음 음절(pa) — 8 ms 터짐 뒤 모음 AA 가 서서히(25 ms) 열린다. @p bBurst 가 false 면 터짐 없이 같은 모음만입니다(a).
         * @details 입술 터짐은 넓은 대역 잡음이되 스펙트럼이 주파수를 따라 떨어진다 — 900 Hz 한 극 낮은 통과 잡음으로 흉내 낸다(영교차는 모음보다 잦다).
         */
        static void appendPlosiveSyllable( vector<float32>& inoutListSample, bool bBurst, float32 seconds )
        {
            const size_t start = inoutListSample.size();
            if ( bBurst )
            {
                vector<float32> listBurst;
                appendLowpassNoise( listBurst, 900.0f, 0.008f );
                for ( const float32 sample : listBurst )
                {
                    inoutListSample.push_back( 1.5f * sample ); // 낮은 통과가 줄인 크기를 되돌린다
                }
            }
            const size_t vowelStart = inoutListSample.size();
            appendVowel( inoutListSample, 750.0f, 1150.0f, seconds - static_cast<float32>( vowelStart - start ) / static_cast<float32>( kSampleRate ) );
            const size_t rampSamples = static_cast<size_t>( 0.025f * static_cast<float32>( kSampleRate ) );
            for ( size_t index = 0; index < rampSamples && vowelStart + index < inoutListSample.size(); ++index )
            {
                inoutListSample[vowelStart + index] *= static_cast<float32>( index ) / static_cast<float32>( rampSamples );
            }
        }

        static void appendSilence( vector<float32>& inoutListSample, float32 seconds )
        {
            inoutListSample.insert( inoutListSample.end(), static_cast<size_t>( seconds * static_cast<float32>( kSampleRate ) ), 0.0f );
        }

        /** @brief 트랙의 시각 @p time 에서 가장 큰 비즘(무음 제외) 이름입니다. */
        static hashed_string findLoudestViseme( const VisemeTrack& track, float32 time )
        {
            vector<float32> listWeight;
            track.sample( time, listWeight );
            uint32 best = 1;
            for ( uint32 visemeIndex = 2; visemeIndex < listWeight.size(); ++visemeIndex )
            {
                if ( listWeight[visemeIndex] > listWeight[best] )
                    best = visemeIndex;
            }
            return track._listViseme[best];
        }

        /** @brief 눈 본의 모델 공간 앞 방향(리그의 앞 축)입니다. */
        static float3 getEyeForward( const SkeletalMeshComponent& unit, const utf8* pBoneName, const float3& forwardAxis, float3& outEyePosition )
        {
            const int32 boneIndex = unit.getSkeleton().findBoneIndex( hashed_string( pBoneName ) );
            if ( boneIndex < 0 )
                return float3{};
            const float4x4& model = unit.getModelSpaceTransforms()[static_cast<size_t>( boneIndex )];
            outEyePosition        = model.getTranslation();
            return float3::transformVector( forwardAxis, model ).normalize();
        }
    };
} // namespace

/**
 * @brief [FacialAnimationTest] 임포트한 테스트 머리는 모프 타깃 일곱(이름 · 순서)과 눈 본을 갖고, Talk 클립은 weights 채널을 타깃 이름의 커브로 싣는다
 * @details 원본은 `models_raw/testhead.gltf`(합성한 머리 — KayKit 은 모프가 없다). 얼굴 리그는 이 메시 · 스켈레톤으로 검증을 통과하고, 없는 타깃을 부르는 리그는 실패한다.
 */
SW_TEST_CASE( FacialAnimationTest, ImportedTestHeadCarriesMorphTargetsAndWeightCurves )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    MeshAssetData mesh{};
    SW_ASSERT_TRUE( MeshAssetFormat::loadFromResource( TestFacialAnimationInternal::kMeshPath, mesh ) );
    SW_EXPECT_TRUE( mesh.hasSkin() );
    const utf8* arrName[7] = { "jawOpen", "mouthWide", "mouthRound", "mouthClose", "smile", "blinkLeft", "blinkRight" };
    SW_ASSERT_EQUAL( size_t( 7 ), mesh._listMorphTarget.size() );
    vector<hashed_string> listTargetName;
    for ( uint32 targetIndex = 0; targetIndex < 7; ++targetIndex )
    {
        SW_EXPECT_TRUE( mesh._listMorphTarget[targetIndex]._name == hashed_string( arrName[targetIndex] ) );
        SW_EXPECT_TRUE( mesh._listMorphTarget[targetIndex]._listDelta.empty() == false );
        listTargetName.push_back( mesh._listMorphTarget[targetIndex]._name );
    }

    Skeleton skeleton;
    SW_ASSERT_TRUE( skeleton.loadFromResource( TestFacialAnimationInternal::kSkeletonPath ) );
    SW_EXPECT_TRUE( skeleton.findBoneIndex( hashed_string( "eye.l" ) ) >= 0 );
    SW_EXPECT_TRUE( skeleton.findBoneIndex( hashed_string( "eye.r" ) ) >= 0 );

    AnimationClip talk;
    SW_ASSERT_TRUE( talk.loadFromResource( string( TestFacialAnimationInternal::kClipFolder ) + "/talk.animclip" ) );
    const AnimationClipCurve* pJaw = talk.findCurve( hashed_string( "jawOpen" ) );
    SW_ASSERT_NOT_NULL( pJaw );
    float32 maxJaw = 0.0f;
    for ( float32 time = 0.0f; time <= talk.getPlayLength(); time += 0.05f )
    {
        maxJaw = MathUtil::max( maxJaw, pJaw->evaluate( time ) );
    }
    SW_EXPECT_TRUE( maxJaw > 0.5f );

    FacialRig rig;
    SW_ASSERT_TRUE( rig.loadFromResource( TestFacialAnimationInternal::kRigPath ) );
    SW_EXPECT_TRUE( rig.validate( listTargetName, skeleton, "testhead" ) );

    test::ScopedDefensiveTestLog expected( "a rig naming a missing morph target fails validation" );
    FacialRig                    broken;
    SW_ASSERT_TRUE( broken.parseJSON( R"({ "_listExpression": [ { "_name": "Frown", "_listTarget": [ { "_target": "browDown", "_weight": 1.0 } ] } ] })", "broken" ) );
    SW_EXPECT_FALSE( broken.validate( listTargetName, skeleton, "broken" ) );
    SW_ASSERT_TRUE( broken.parseJSON( R"({ "_listExpression": [ { "_name": "Smile", "_listTarget": [ { "_target": "smile", "_weight": 1.0 } ] } ] })", "clash" ) );
    SW_EXPECT_FALSE( broken.validate( listTargetName, skeleton, "clash" ) );
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "_listExpression": [], "eyebrows": {} })", "unknown key" ) );
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "_listViseme": [ { "_name": "AA" }, { "_name": "AA" } ] })", "duplicate pose" ) );
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "_blink": { "_minInterval": 3.0, "_maxInterval": 1.0 } })", "unordered blink" ) );
    SW_EXPECT_TRUE( broken.getExpressions().empty() );
}

/**
 * @brief [FacialAnimationTest] 선형 예측 포락선의 두 봉우리가 합성 모음의 포먼트에 닿는다 — 첫째 · 둘째 포먼트가 실제 값의 20 % 안이다
 * @details 모음 셋(AA 750/1150, EE 300/2300, OO 320/800 Hz)을 한 프레임(1/30 초) 창으로 잰다. 무음 · 너무 짧은 창은 잴 수 없다.
 */
SW_TEST_CASE( FacialAnimationTest, LipSyncFormantsTrackSyntheticVowels )
{
    const float2 arrFormant[3] = {
        float2{750.0f, 1150.0f},
        float2{300.0f, 2300.0f},
        float2{320.0f,  800.0f}
    };
    const size_t windowSamples = TestFacialAnimationInternal::kSampleRate / 30u;
    for ( const float2& formant : arrFormant )
    {
        vector<float32> listSample;
        TestFacialAnimationInternal::appendVowel( listSample, formant._x, formant._y, 0.2f );
        float2 measured{};
        SW_ASSERT_TRUE( LipSyncAnalyzer::tryComputeFormants( &listSample[windowSamples], windowSamples, TestFacialAnimationInternal::kSampleRate, measured ) );
        SW_EXPECT_NEAR_EQUAL( formant._x, measured._x, formant._x * 0.2f );
        SW_EXPECT_NEAR_EQUAL( formant._y, measured._y, formant._y * 0.2f );
    }
    vector<float32> listSilence;
    TestFacialAnimationInternal::appendSilence( listSilence, 0.1f );
    float2 unused{};
    SW_EXPECT_FALSE( LipSyncAnalyzer::tryComputeFormants( listSilence.data(), listSilence.size(), TestFacialAnimationInternal::kSampleRate, unused ) );
    SW_EXPECT_FALSE( LipSyncAnalyzer::tryComputeFormants( listSilence.data(), 8, TestFacialAnimationInternal::kSampleRate, unused ) );
}

/**
 * @brief [FacialAnimationTest] 립싱크 분석은 합성 모음 · 치찰음 · 무음 구간을 표의 비즘(AA · EE · OO · SS · sil)으로 가르고, 트랙은 JSON 으로 그대로 오간다
 * @details 모음은 두 포먼트 봉우리의 배음 합(AA 750/1150 Hz, EE 300/2300, OO 320/800), 치찰음은 높은 대역 잡음이다. 구간 가운데 프레임을 본다.
 */
SW_TEST_CASE( FacialAnimationTest, LipSyncAnalyzerSeparatesSyntheticVowels )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    LipSyncSettings settings;
    SW_ASSERT_TRUE( settings.loadFromResource( LipSyncSettings::kResourcePath ) );

    vector<float32> listSample;
    TestFacialAnimationInternal::appendSilence( listSample, 0.4f );
    TestFacialAnimationInternal::appendVowel( listSample, 750.0f, 1150.0f, 0.4f );
    TestFacialAnimationInternal::appendVowel( listSample, 300.0f, 2300.0f, 0.4f );
    TestFacialAnimationInternal::appendVowel( listSample, 320.0f, 800.0f, 0.4f );
    TestFacialAnimationInternal::appendHiss( listSample, 0.4f );
    TestFacialAnimationInternal::appendSilence( listSample, 0.4f );

    VisemeTrack track;
    LipSyncAnalyzer::analyze( listSample, TestFacialAnimationInternal::kSampleRate, settings, track );
    SW_ASSERT_EQUAL( settings._listViseme.size(), track._listViseme.size() );
    SW_EXPECT_TRUE( track.getDuration() > 2.3f && track.getDuration() < 2.5f );

    vector<float32> listWeight;
    track.sample( 0.2f, listWeight );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listWeight[0], 0.001f );
    SW_EXPECT_TRUE( TestFacialAnimationInternal::findLoudestViseme( track, 0.6f ) == hashed_string( "AA" ) );
    SW_EXPECT_TRUE( TestFacialAnimationInternal::findLoudestViseme( track, 1.0f ) == hashed_string( "EE" ) );
    SW_EXPECT_TRUE( TestFacialAnimationInternal::findLoudestViseme( track, 1.4f ) == hashed_string( "OO" ) );
    SW_EXPECT_TRUE( TestFacialAnimationInternal::findLoudestViseme( track, 1.8f ) == hashed_string( "SS" ) );
    track.sample( 2.2f, listWeight );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listWeight[0], 0.001f );
    // 소리가 난 구간은 무음 가중치가 1 보다 작다(입이 열린다).
    track.sample( 0.6f, listWeight );
    SW_EXPECT_TRUE( listWeight[0] < 0.9f );

    // 진폭 립싱크: 0.5 진폭 모음은 full 위라 다 열리고, 무음은 닫힌다.
    const float32 loud = LipSyncAnalyzer::computeRms( listSample, TestFacialAnimationInternal::kSampleRate, 0.6f, 1.0f / 30.0f );
    SW_EXPECT_TRUE( LipSyncAnalyzer::computeOpenness( loud, settings ) > 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, LipSyncAnalyzer::computeOpenness( 0.0f, settings ), 0.0f );

    VisemeTrack copy;
    SW_ASSERT_TRUE( copy.parseJSON( track.toJSON(), "roundtrip" ) );
    SW_EXPECT_EQUAL( track.getFrameCount(), copy.getFrameCount() );
    SW_EXPECT_TRUE( copy._listViseme == track._listViseme );
    for ( size_t index = 0; index < track._listWeight.size(); ++index )
    {
        SW_EXPECT_NEAR_EQUAL( track._listWeight[index], copy._listWeight[index], 0.0006f );
    }
    SW_EXPECT_TRUE( VisemeTrack::makePathForAudio( "game/empty/voice/line.wav" ) == "game/empty/voice/line.visemes.json" );
    SW_EXPECT_TRUE( LipSyncImport::isVoiceAudio( "game/empty/voice/line.wav" ) );
    SW_EXPECT_TRUE( LipSyncImport::isVoiceAudio( "voice/line.ogg" ) );
    SW_EXPECT_FALSE( LipSyncImport::isVoiceAudio( "game/empty/sounds/click.ogg" ) );
    SW_EXPECT_FALSE( LipSyncImport::isVoiceAudio( "game/empty/voice/line.visemes.json" ) );
    // 스테레오는 채널 평균으로 모노가 된다(분석 입력).
    AudioClipData stereo{};
    stereo._channelCount = 2;
    stereo._frameCount   = 2;
    stereo._sampleRate   = 16000;
    stereo._listSample   = { 1.0f, 0.0f, -0.5f, 0.5f };
    vector<float32> listMono;
    stereo.copyMonoSamples( listMono );
    SW_ASSERT_EQUAL( size_t( 2 ), listMono.size() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listMono[0], 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listMono[1], 0.0f );

    test::ScopedDefensiveTestLog expected( "unknown keys in lip sync data are rejected" );
    LipSyncSettings              broken;
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "_frameRate": 30, "pitch": 1 })", "broken" ) );
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "_listViseme": [ { "_name": "sil" }, { "_name": "AA" } ], "_fallbackViseme": "OO" })", "unknown fallback" ) );
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "_listViseme": [ { "_name": "sil" }, { "_name": "AA" } ], "_fallbackViseme": "AA", "_lowBandHz": "600,150" })",
                                       "reversed band" ) );
    SW_EXPECT_TRUE( broken._listViseme.empty() );
    SW_EXPECT_FALSE( copy.parseJSON( R"({ "frame_rate": 30, "visemes": [ "sil" ], "frames": [ [ 0 ] ], "phonemes": [] })", "broken" ) );
}

/**
 * @brief [FacialAnimationTest] 자음 단서 — 무음 뒤 짧은 터짐은 파열음(PP), 영교차가 잦은 낮은 대역 잡음은 마찰음(FF)이다. 터짐 없이 서서히 열리는 같은 모음은 PP 가 아니다
 * @details 낮은 통과 잡음은 높은 대역이 주가 아니라 대역 형태만으로는 유성으로 보여 잡음 포락선의 봉우리를 포먼트로 잴 뻔한다 — 마찰음 단서가 포먼트 비교를 끈다.
 */
SW_TEST_CASE( FacialAnimationTest, LipSyncConsonantCuesMarkPlosivesAndFricatives )
{
    using Internal = TestFacialAnimationInternal;
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    LipSyncSettings settings;
    SW_ASSERT_TRUE( settings.loadFromResource( LipSyncSettings::kResourcePath ) );

    // 0.3 초에 "pa", 1.0 초에 "a", 1.7 초부터 "f" — 각 구간 앞은 무음이다(프레임 9 · 30 의 첫 표본에서 시작).
    vector<float32> listSample;
    Internal::appendSilence( listSample, 0.3f );
    Internal::appendPlosiveSyllable( listSample, true, 0.4f );
    Internal::appendSilence( listSample, 0.3f );
    Internal::appendPlosiveSyllable( listSample, false, 0.4f );
    Internal::appendSilence( listSample, 0.3f );
    Internal::appendLowpassNoise( listSample, 1200.0f, 0.4f );
    VisemeTrack track;
    LipSyncAnalyzer::analyze( listSample, Internal::kSampleRate, settings, track );

    SW_EXPECT_TRUE( Internal::findLoudestViseme( track, 0.3f ) == hashed_string( "PP" ) );
    SW_EXPECT_TRUE( Internal::findLoudestViseme( track, 0.5f ) == hashed_string( "AA" ) ); // 터짐 뒤 모음은 그대로 모음이다
    SW_EXPECT_TRUE( Internal::findLoudestViseme( track, 1.0f ) != hashed_string( "PP" ) );
    SW_EXPECT_TRUE( Internal::findLoudestViseme( track, 1.2f ) == hashed_string( "AA" ) );
    SW_EXPECT_TRUE( Internal::findLoudestViseme( track, 1.9f ) == hashed_string( "FF" ) );

    test::ScopedDefensiveTestLog expected( "a consonant cue on a vowel viseme is rejected" );
    LipSyncSettings              broken;
    SW_EXPECT_FALSE( broken.parseJSON(
        R"({ "_listViseme": [ { "_name": "sil" }, { "_name": "AA", "_formantHz": "730,1090", "_consonant": "Plosive" } ], "_fallbackViseme": "AA" })", "vowel cue" ) );
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "_listViseme": [ { "_name": "sil" }, { "_name": "PP", "_consonant": "Pop" } ], "_fallbackViseme": "PP" })", "unknown cue" ) );
}

/**
 * @brief [FacialAnimationTest] 표정 가중치와 애니메이터의 같은 이름 커브가 리그의 타깃 가중치를 곱해 유닛 모프 가중치가 된다 — 타깃 이름 커브는 그대로 가중치다
 * @details Happy 1 → smile 1 · mouthWide 0.3. 표정 이름은 타깃 이름과 겹치지 않는다(겹치면 리그 검증 오류 — 이름은 대소문자를 무시한다). Talk 상태의 애니메이터는 jawOpen 커브(타깃 이름)를 내고, 그것이 유닛의 jawOpen 가중치와 같다.
 */
SW_TEST_CASE( FacialAnimationTest, ExpressionsAndCurvesDriveMorphWeights )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit, "Talk" );
    SW_ASSERT_NOT_NULL( pFace );
    SW_ASSERT_EQUAL( 7u, pUnit->getMorphTargetCount() );

    SW_EXPECT_TRUE( pFace->setExpressionWeight( hashed_string( "Happy" ), 1.0f ) );
    SW_EXPECT_FALSE( pFace->setExpressionWeight( hashed_string( "Anger" ), 1.0f ) );
    manager.getSubsystem<AnimationSystem>().evaluate( 0.5f );
    // Talk 클립은 일곱 타깃 모두의 커브를 싣는다 — 기대값은 표정 몫 + 그 커브다.
    const SkeletalAnimatorComponent* pAnimator = pUnit->getOwner()->getComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pAnimator );
    SW_EXPECT_NEAR_EQUAL( 1.0f + pAnimator->getCurveValue( hashed_string( "smile" ) ), TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );
    // mouthWide = Happy 의 0.3 + Talk 클립의 mouthWide 커브.
    const float32 talkWide = pAnimator->getCurveValue( hashed_string( "mouthWide" ) );
    SW_EXPECT_NEAR_EQUAL( 0.3f + talkWide, TestFacialAnimationInternal::getWeight( *pUnit, "mouthWide" ), 0.0001f );
    const float32 talkJaw = pAnimator->getCurveValue( hashed_string( "jawOpen" ) );
    SW_EXPECT_TRUE( talkJaw > 0.0f );
    SW_EXPECT_NEAR_EQUAL( talkJaw, TestFacialAnimationInternal::getWeight( *pUnit, "jawOpen" ), 0.0001f );

    // 표정을 내리면 다음 평가에서 빠진다(가중치는 프레임마다 새로 만든다).
    SW_EXPECT_TRUE( pFace->setExpressionWeight( hashed_string( "Happy" ), 0.0f ) );
    manager.getSubsystem<AnimationSystem>().evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( pAnimator->getCurveValue( hashed_string( "smile" ) ), TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );
}

/**
 * @brief [FacialAnimationTest] 비즘 트랙은 프레임마다 리그의 비즘 포즈를 열고, 진폭 표본은 대체 비즘(AA → jawOpen)을 연다 — 끝나면 입이 닫힌다
 */
SW_TEST_CASE( FacialAnimationTest, SpeechOpensTheMouthFromTrackOrAmplitude )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit );
    SW_ASSERT_NOT_NULL( pFace );
    LipSyncSettings settings;
    SW_ASSERT_TRUE( settings.loadFromResource( LipSyncSettings::kResourcePath ) );
    pFace->setLipSyncSettings( settings );
    AnimationSystem& system = manager.getSubsystem<AnimationSystem>();

    VisemeTrack track;
    track._frameRate  = 10.0f;
    track._listViseme = { hashed_string( "sil" ), hashed_string( "AA" ), hashed_string( "OO" ) };
    track._listWeight = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    pFace->speakTrack( track );
    SW_EXPECT_TRUE( pFace->isSpeaking() );
    system.evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, TestFacialAnimationInternal::getWeight( *pUnit, "jawOpen" ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, TestFacialAnimationInternal::getWeight( *pUnit, "mouthRound" ), 0.001f );
    system.evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, TestFacialAnimationInternal::getWeight( *pUnit, "jawOpen" ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, TestFacialAnimationInternal::getWeight( *pUnit, "mouthRound" ), 0.001f );
    system.evaluate( 0.2f );
    SW_EXPECT_FALSE( pFace->isSpeaking() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, TestFacialAnimationInternal::getWeight( *pUnit, "jawOpen" ), 0.0f );

    // 진폭 립싱크 — 0.5 초 소리, 0.5 초 무음.
    vector<float32> listSample;
    TestFacialAnimationInternal::appendVowel( listSample, 750.0f, 1150.0f, 0.5f );
    TestFacialAnimationInternal::appendSilence( listSample, 0.5f );
    pFace->speakSamples( listSample, TestFacialAnimationInternal::kSampleRate );
    system.evaluate( 0.25f );
    const float32 expected = LipSyncAnalyzer::computeOpenness(
        LipSyncAnalyzer::computeRms( listSample, TestFacialAnimationInternal::kSampleRate, 0.25f, 1.0f / settings._frameRate ), settings );
    SW_EXPECT_TRUE( expected > 0.3f );
    SW_EXPECT_NEAR_EQUAL( expected, TestFacialAnimationInternal::getWeight( *pUnit, "jawOpen" ), 0.001f );
    system.evaluate( 0.5f );
    SW_EXPECT_TRUE( pFace->isSpeaking() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, TestFacialAnimationInternal::getWeight( *pUnit, "jawOpen" ), 0.0f );

    // 트랙 파일이 곁에 있는 음성은 그 트랙을 말한다(임포트 결과).
    SW_EXPECT_TRUE( pFace->speak( "game/empty/voice/testline.wav" ) );
    SW_EXPECT_TRUE( pFace->isSpeaking() );
}

/**
 * @brief [FacialAnimationTest] 깜빡임은 리그 간격(2 ~ 5 초) · 길이(0.15 초)대로 두 눈꺼풀 타깃을 올렸다 내린다 — 10 초에 2 ~ 5 번, 사이에는 0
 */
SW_TEST_CASE( FacialAnimationTest, BlinkFollowsTheRigIntervals )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit );
    SW_ASSERT_NOT_NULL( pFace );
    pFace->setAutoBlink( true );

    const float32 stepSeconds = 1.0f / 60.0f;
    uint32        blinkCount  = 0;
    bool          bClosing    = false;
    float32       maxWeight   = 0.0f;
    for ( uint32 frame = 0; frame < 600; ++frame )
    {
        manager.getSubsystem<AnimationSystem>().evaluate( stepSeconds );
        const float32 left  = TestFacialAnimationInternal::getWeight( *pUnit, "blinkLeft" );
        const float32 right = TestFacialAnimationInternal::getWeight( *pUnit, "blinkRight" );
        SW_EXPECT_NEAR_EQUAL( left, right, 0.0f );
        SW_EXPECT_NEAR_EQUAL( pFace->getBlinkAmount(), left, 0.0f );
        maxWeight = MathUtil::max( maxWeight, left );
        if ( left > 0.0f && bClosing == false )
            ++blinkCount;
        bClosing = left > 0.0f;
    }
    SW_EXPECT_TRUE( maxWeight > 0.8f );
    SW_EXPECT_TRUE( blinkCount >= 2 && blinkCount <= 5 );
}

/**
 * @brief [FacialAnimationTest] 시선 — 눈 본의 앞 축이 범위 안 목표를 정확히 보고, 범위 밖 목표는 최대 각(30°)에서 멈춘다. 놓으면 레퍼런스로 돌아간다
 */
SW_TEST_CASE( FacialAnimationTest, GazeTurnsEyesTowardTheTargetWithinTheLimit )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit );
    SW_ASSERT_NOT_NULL( pFace );
    AnimationSystem& system      = manager.getSubsystem<AnimationSystem>();
    const float3     forwardAxis = pFace->getFacialRig().getGaze()._forwardAxis;

    system.evaluate( 0.0f );
    float3       eyePosition{};
    const float3 restForward = TestFacialAnimationInternal::getEyeForward( *pUnit, "eye.l", forwardAxis, eyePosition );
    SW_ASSERT_TRUE( restForward.getLengthSquared() > 0.5f );

    // 앞에서 약 11° 옆 — 범위 안이라 정확히 본다.
    const float3 side    = restForward.cross( float3{ 0.0f, 1.0f, 0.0f } ).normalize();
    const float3 nearAim = eyePosition + restForward + side * 0.2f;
    pFace->setLookAtTarget( nearAim );
    system.evaluate( 1.0f / 60.0f );
    float3       turnedPosition{};
    const float3 turned = TestFacialAnimationInternal::getEyeForward( *pUnit, "eye.l", forwardAxis, turnedPosition );
    SW_EXPECT_TRUE( turned.dot( ( nearAim - turnedPosition ).normalize() ) > 0.9999f );

    // 옆 60° — 30° 에서 멈춘다(레퍼런스 앞에서 측정한다).
    pFace->setLookAtTarget( eyePosition + restForward + side * MathUtil::tan( MathUtil::toRadian( 60.0f ) ) );
    system.evaluate( 1.0f / 60.0f );
    const float3  clamped = TestFacialAnimationInternal::getEyeForward( *pUnit, "eye.l", forwardAxis, turnedPosition );
    const float32 angle   = MathUtil::toDegree( MathUtil::acos( MathUtil::clamp( clamped.dot( restForward ), -1.0f, 1.0f ) ) );
    SW_EXPECT_NEAR_EQUAL( 30.0f, angle, 0.1f );
    SW_EXPECT_TRUE( clamped.dot( side ) > 0.0f );

    pFace->clearLookAtTarget();
    system.evaluate( 1.0f / 60.0f );
    const float3 released = TestFacialAnimationInternal::getEyeForward( *pUnit, "eye.l", forwardAxis, turnedPosition );
    SW_EXPECT_TRUE( released.dot( restForward ) > 0.9999f );
}

/**
 * @brief [FacialAnimationTest] 애니메이터 클립의 표정 이름 커브(`Happy`)가 표정 가중치가 된다 — 언리얼 MetaHuman 의 표정 커브와 같은 자리
 * @details Talk 클립에 `Happy` 0.5 커브를 더한 `Grin` 클립을 임시 폴더에 쓰고 그 상태로 재생한다. smile = 0.5 × 1(리그) + Talk 의 smile 커브.
 */
SW_TEST_CASE( FacialAnimationTest, AnimatorExpressionCurvesDriveExpressions )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    AnimationClip grin;
    SW_ASSERT_TRUE( grin.loadFromResource( string( TestFacialAnimationInternal::kClipFolder ) + "/talk.animclip" ) );
    grin.setName( hashed_string( "Grin" ) );
    AnimationClipCurve happy{};
    happy._name = hashed_string( "Happy" );
    happy._listKey.push_back( AnimationClipCurveKey{ 0.0f, 0.5f } );
    happy._listKey.push_back( AnimationClipCurveKey{ grin.getPlayLength(), 0.5f } );
    grin.addCurve( happy );
    const string folder = test::makeTempPath( "grinclips" );
    SW_ASSERT_TRUE( grin.saveToFile( FileUtil::joinPath( folder, "grin.animclip" ) ) );

    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit, "Grin", ::test::makeTempResourceID( folder ) );
    SW_ASSERT_NOT_NULL( pFace );
    manager.getSubsystem<AnimationSystem>().evaluate( 0.5f );
    const SkeletalAnimatorComponent* pAnimator = pUnit->getOwner()->getComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pAnimator );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pAnimator->getCurveValue( hashed_string( "Happy" ) ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f + pAnimator->getCurveValue( hashed_string( "smile" ) ), TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.15f + pAnimator->getCurveValue( hashed_string( "mouthWide" ) ), TestFacialAnimationInternal::getWeight( *pUnit, "mouthWide" ), 0.0001f );
}

/**
 * @brief [FacialAnimationTest] 스킨 없는 모프 메시도 유닛이 본 하나의 스킨 원본으로 바꿔 모프 가중치를 받는다 — 정점 · 타깃은 그대로다
 * @details GPU 모프 가중치는 스키닝 컴퓨트 안에서만 걸린다. 스킨이 없으면 그 길에 들지 못하므로 `Mesh::createRigidSkinSource` 가 고정 본을 준다.
 */
SW_TEST_CASE( FacialAnimationTest, UnskinnedMorphMeshGetsARigidSkin )
{
    shared_ptr<Mesh> cube = MeshUtil::createUnitCube();
    MeshMorphTarget  bulge{};
    bulge._name = hashed_string( "bulge" );
    bulge._listDelta.push_back( MeshMorphDelta{
        0u, float3{ 0.0f, 0.5f, 0.0f },
        float3{}
    } );
    cube->setMorphTargets( { bulge } );
    SW_ASSERT_FALSE( cube->hasSkin() );

    GameObjectManager      manager;
    GameObject*            pObject = manager.createGameObject( hashed_string( "Prop" ) );
    SkeletalMeshComponent* pUnit   = pObject->addComponent<SkeletalMeshComponent>();
    SW_ASSERT_NOT_NULL( pUnit );
    pUnit->setMesh( cube );
    pUnit->resolveRenderAssets();
    SW_ASSERT_NOT_NULL( pUnit->getMesh() );
    SW_EXPECT_TRUE( pUnit->getMesh()->hasSkin() );
    SW_EXPECT_EQUAL( 1u, pUnit->getMesh()->getSkinBoneCount() );
    SW_EXPECT_EQUAL( cube->getVertices().size(), pUnit->getMesh()->getVertices().size() );
    SW_EXPECT_EQUAL( 0, pUnit->findMorphTargetIndex( hashed_string( "bulge" ) ) );
}

/**
 * @brief [FacialAnimationTest] Live Link Face 패킷 — 쓰고 푼 값이 같고, 다른 판 · 모자란 · 남는 패킷은 까닭과 함께 거절한다. 연결 표는 배치 표에 없는 채널을 오류로 본다
 */
SW_TEST_CASE( FacialAnimationTest, LiveLinkFacePacketsParseAgainstTheDataLayout )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    LiveLinkFaceLayout layout;
    SW_ASSERT_TRUE( layout.loadFromResource( TestFacialAnimationInternal::kLiveLinkLayoutPath ) );
    SW_ASSERT_EQUAL( size_t( 61 ), layout._listChannel.size() );
    const int32 jawOpen = layout.findChannelIndex( hashed_string( "JawOpen" ) );
    SW_ASSERT_TRUE( jawOpen >= 0 );

    LiveLinkFaceFrame frame;
    frame._deviceID    = "A1B2C3D4";
    frame._subjectName = "iPhone";
    frame._frameNumber = 1234;
    frame._listValue.assign( layout._listChannel.size(), 0.0f );
    frame._listValue[static_cast<size_t>( jawOpen )] = 0.75f;
    vector<uint8> listByte;
    LiveLinkFacePacket::write( frame, static_cast<uint8>( layout._packetVersion ), listByte );
    LiveLinkFaceFrame parsed;
    string            error;
    SW_ASSERT_TRUE( LiveLinkFacePacket::parse( listByte.data(), listByte.size(), layout, parsed, error ) );
    SW_EXPECT_TRUE( parsed._subjectName == "iPhone" );
    SW_EXPECT_TRUE( parsed._deviceID == "A1B2C3D4" );
    SW_EXPECT_EQUAL( 1234, parsed._frameNumber );
    SW_EXPECT_NEAR_EQUAL( 0.75f, parsed._listValue[static_cast<size_t>( jawOpen )], 0.0f );

    // 다른 판 · 잘린 패킷 · 꼬리가 남는 패킷은 거절한다.
    vector<uint8> listOther;
    LiveLinkFacePacket::write( frame, static_cast<uint8>( layout._packetVersion + 1 ), listOther );
    SW_EXPECT_FALSE( LiveLinkFacePacket::parse( listOther.data(), listOther.size(), layout, parsed, error ) );
    SW_EXPECT_TRUE( error.find( "version" ) != string::npos );
    SW_EXPECT_FALSE( LiveLinkFacePacket::parse( listByte.data(), listByte.size() - 2, layout, parsed, error ) );
    SW_EXPECT_TRUE( parsed._listValue.empty() );
    listOther = listByte;
    listOther.push_back( 0 );
    SW_EXPECT_FALSE( LiveLinkFacePacket::parse( listOther.data(), listOther.size(), layout, parsed, error ) );
    frame._listValue.pop_back();
    LiveLinkFacePacket::write( frame, static_cast<uint8>( layout._packetVersion ), listOther );
    SW_EXPECT_FALSE( LiveLinkFacePacket::parse( listOther.data(), listOther.size(), layout, parsed, error ) );
    SW_EXPECT_TRUE( error.find( "channels" ) != string::npos );

    test::ScopedDefensiveTestLog expected( "a mapping naming a channel the layout lacks is rejected" );
    LiveLinkFaceMapping          mapping;
    LiveLinkFaceLayout           mappedLayout;
    SW_EXPECT_FALSE( mapping.parseJSON( R"({ "_layoutPath": "engine/animation/livelinkface.json", "_listExpression": [ { "_expression": "Happy", "_listChannel": [ { "_channel": "Grin" } ] } ] })", "unknown channel", mappedLayout ) );
    SW_EXPECT_FALSE( mapping.parseJSON( R"({ "_layoutPath": "engine/animation/livelinkface.json", "_listExpression": [ { "_expression": "Happy", "_listChannel": [] } ] })", "empty expression", mappedLayout ) );
    SW_EXPECT_TRUE( mapping._listExpression.empty() );
}

/**
 * @brief [FacialAnimationTest] Live Link Face 컴포넌트가 이 기계의 UDP 포트로 받은 패킷을 머리의 연결 표대로 표정에 건다 — 웃음 좌우 채널 0.8 → Happy 0.8 → smile 0.8
 * @details 포트는 운영체제가 고르고(0) 이 기계의 송신만 받는다. 실제 Live Link Face 앱 대신 같은 형식으로 쓴 패킷을 보낸다.
 */
SW_TEST_CASE( FacialAnimationTest, LiveLinkFaceComponentDrivesExpressionsFromUDP )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit );
    SW_ASSERT_NOT_NULL( pFace );
    LiveLinkFaceComponent* pLink = pFace->getOwner()->addComponent<LiveLinkFaceComponent>();
    SW_ASSERT_NOT_NULL( pLink );
    pLink->setMappingPath( "game/empty/models/testhead.livelinkface.json" );
    pLink->setPort( 0 );
    pLink->setLoopbackOnly( true );
    pLink->dispatchBeginPlay();
    SW_ASSERT_TRUE( pLink->getBoundPort() != 0 );

    LiveLinkFaceLayout layout;
    SW_ASSERT_TRUE( layout.loadFromResource( TestFacialAnimationInternal::kLiveLinkLayoutPath ) );
    LiveLinkFaceFrame frame;
    frame._subjectName = "iPhone";
    frame._listValue.assign( layout._listChannel.size(), 0.0f );
    frame._listValue[static_cast<size_t>( layout.findChannelIndex( hashed_string( "MouthSmileLeft" ) ) )]  = 0.8f;
    frame._listValue[static_cast<size_t>( layout.findChannelIndex( hashed_string( "MouthSmileRight" ) ) )] = 0.8f;
    vector<uint8> listByte;
    LiveLinkFacePacket::write( frame, static_cast<uint8>( layout._packetVersion ), listByte );
    UDPNetTransport sender;
    SW_ASSERT_TRUE( sender.open( 0, true ) );
    SW_ASSERT_TRUE( sender.send( NetAddress::makeLoopback( pLink->getBoundPort() ), listByte.data(), static_cast<int32>( listByte.size() ) ) );

    // 데이터그램이 도착할 때까지 틱한다(같은 기계라 곧 온다).
    for ( uint32 attempt = 0; attempt < 200 && pLink->getAppliedPacketCount() == 0; ++attempt )
    {
        pLink->onTick( 0.0f );
        if ( pLink->getAppliedPacketCount() == 0 )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    SW_ASSERT_EQUAL( 1u, pLink->getAppliedPacketCount() );
    manager.getSubsystem<AnimationSystem>().evaluate( 1.0f / 30.0f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, TestFacialAnimationInternal::getWeight( *pUnit, "jawOpen" ), 0.0001f );
    sender.close();
}

/**
 * @brief [FacialAnimationTest] 실시간 얼굴 입력의 표정 채널이 표정 가중치에 더해지고, 떼면 사라진다 — Live Link Face 자리
 * @details 입력이 `Happy` 0.5 를 내면 smile = 0.5 × 1(리그), mouthWide = 0.5 × 0.3. 다음 프레임에 바꾼 값이 들어가고, 떼면 0 이다.
 */
SW_TEST_CASE( FacialAnimationTest, LiveSourceChannelsDriveExpressions )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit );
    SW_ASSERT_NOT_NULL( pFace );
    TestFacialLiveSource source;
    source._happy = 0.5f;
    pFace->registerLiveSource( &source );
    AnimationSystem& system = manager.getSubsystem<AnimationSystem>();
    system.evaluate( 1.0f / 30.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.15f, TestFacialAnimationInternal::getWeight( *pUnit, "mouthWide" ), 0.0001f );

    source._happy = 1.0f;
    system.evaluate( 1.0f / 30.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );

    pFace->unregisterLiveSource();
    system.evaluate( 1.0f / 30.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );
}
