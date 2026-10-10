#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/LipSyncImport.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/3D/FacialAnimationComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// FacialAnimationTest — 모프 타깃 임포트 결과 · 립싱크 분석 · 얼굴 리그(표정 · 비즘 · 깜빡임 · 시선)가 유닛의 모프 가중치 · 눈 본에 닿는 것.

namespace
{
    struct TestFacialAnimationInternal
    {
        static constexpr string_view kMeshPath     = "game/empty/models/testhead.mesh";
        static constexpr string_view kSkeletonPath = "game/empty/models/testhead/testhead.skeleton.json";
        static constexpr string_view kRigPath      = "game/empty/models/testhead.facial.json";
        static constexpr string_view kClipFolder   = "game/empty/models/testhead/clips";
        static constexpr uint32      kSampleRate   = 16000;

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

        /** @brief 두 포먼트 봉우리로 모양을 낸 120 Hz 목소리(배음 합)입니다 — 모음 하나를 흉내 냅니다. */
        static void appendVowel( vector<float32>& inoutListSample, float32 firstFormant, float32 secondFormant, float32 seconds )
        {
            auto resonance = []( float32 frequency, float32 centre, float32 width )
            {
                const float32 offset = ( frequency - centre ) / width;
                return 1.0f / ( 1.0f + offset * offset );
            };
            vector<float32> listHarmonicFrequency;
            vector<float32> listHarmonicAmplitude;
            float32         amplitudeSum = 0.0f;
            for ( float32 frequency = 120.0f; frequency < 7000.0f; frequency += 120.0f )
            {
                const float32 amplitude = resonance( frequency, firstFormant, 120.0f ) + 0.7f * resonance( frequency, secondFormant, 180.0f );
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
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
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

    AnimClip talk;
    SW_ASSERT_TRUE( talk.loadFromResource( string( TestFacialAnimationInternal::kClipFolder ) + "/talk.animclip" ) );
    const AnimCurve* pJaw = talk.findCurve( hashed_string( "jawOpen" ) );
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
    SW_ASSERT_TRUE( broken.parseJSON( R"({ "expressions": { "Frown": { "browDown": 1.0 } } })", "broken" ) );
    SW_EXPECT_FALSE( broken.validate( listTargetName, skeleton, "broken" ) );
    SW_ASSERT_TRUE( broken.parseJSON( R"({ "expressions": { "Smile": { "smile": 1.0 } } })", "clash" ) );
    SW_EXPECT_FALSE( broken.validate( listTargetName, skeleton, "clash" ) );
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "expressions": {}, "eyebrows": {} })", "unknown key" ) );
}

/**
 * @brief [FacialAnimationTest] 립싱크 분석은 합성 모음 · 치찰음 · 무음 구간을 표의 비즘(AA · EE · OO · SS · sil)으로 가르고, 트랙은 JSON 으로 그대로 오간다
 * @details 모음은 두 포먼트 봉우리의 배음 합(AA 750/1150 Hz, EE 300/2300, OO 320/800), 치찰음은 높은 대역 잡음이다. 구간 가운데 프레임을 본다.
 */
SW_TEST_CASE( FacialAnimationTest, LipSyncAnalyzerSeparatesSyntheticVowels )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
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
    SW_EXPECT_FALSE( broken.parseJSON( R"({ "frame_rate": 30, "pitch": 1 })", "broken" ) );
    SW_EXPECT_FALSE( copy.parseJSON( R"({ "frame_rate": 30, "visemes": [ "sil" ], "frames": [ [ 0 ] ], "phonemes": [] })", "broken" ) );
}

/**
 * @brief [FacialAnimationTest] 표정 가중치와 애니메이터의 같은 이름 커브가 리그의 타깃 가중치를 곱해 유닛 모프 가중치가 된다 — 타깃 이름 커브는 그대로 가중치다
 * @details Happy 1 → smile 1 · mouthWide 0.3. 표정 이름은 타깃 이름과 겹치지 않는다(겹치면 리그 검증 오류 — 이름은 대소문자를 무시한다). Talk 상태의 애니메이터는 jawOpen 커브(타깃 이름)를 내고, 그것이 유닛의 jawOpen 가중치와 같다.
 */
SW_TEST_CASE( FacialAnimationTest, ExpressionsAndCurvesDriveMorphWeights )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit, "Talk" );
    SW_ASSERT_NOT_NULL( pFace );
    SW_ASSERT_EQUAL( 7u, pUnit->getMorphTargetCount() );

    SW_EXPECT_TRUE( pFace->setExpressionWeight( hashed_string( "Happy" ), 1.0f ) );
    SW_EXPECT_FALSE( pFace->setExpressionWeight( hashed_string( "Anger" ), 1.0f ) );
    manager.getAnimationSystem().evaluate( 0.5f );
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
    manager.getAnimationSystem().evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( pAnimator->getCurveValue( hashed_string( "smile" ) ), TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );
}

/**
 * @brief [FacialAnimationTest] 비즘 트랙은 프레임마다 리그의 비즘 포즈를 열고, 진폭 표본은 대체 비즘(AA → jawOpen)을 연다 — 끝나면 입이 닫힌다
 */
SW_TEST_CASE( FacialAnimationTest, SpeechOpensTheMouthFromTrackOrAmplitude )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit );
    SW_ASSERT_NOT_NULL( pFace );
    LipSyncSettings settings;
    SW_ASSERT_TRUE( settings.loadFromResource( LipSyncSettings::kResourcePath ) );
    pFace->setLipSyncSettings( settings );
    AnimationSystem& system = manager.getAnimationSystem();

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
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
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
        manager.getAnimationSystem().evaluate( stepSeconds );
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
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit );
    SW_ASSERT_NOT_NULL( pFace );
    AnimationSystem& system      = manager.getAnimationSystem();
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

    // 옆 60° — 30° 에서 멈춘다(레퍼런스 앞에서 잰다).
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
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    AnimClip grin;
    SW_ASSERT_TRUE( grin.loadFromResource( string( TestFacialAnimationInternal::kClipFolder ) + "/talk.animclip" ) );
    grin.setName( hashed_string( "Grin" ) );
    AnimCurve happy{};
    happy._name = hashed_string( "Happy" );
    happy._listKey.push_back( AnimCurveKey{ 0.0f, 0.5f } );
    happy._listKey.push_back( AnimCurveKey{ grin.getPlayLength(), 0.5f } );
    grin.addCurve( happy );
    const string folder = test::makeTempPath( "grinclips" );
    SW_ASSERT_TRUE( grin.saveToFile( FileUtil::joinPath( folder, "grin.animclip" ) ) );

    GameObjectManager         manager;
    SkeletalMeshComponent*    pUnit = nullptr;
    FacialAnimationComponent* pFace = TestFacialAnimationInternal::createHead( manager, pUnit, "Grin", folder );
    SW_ASSERT_NOT_NULL( pFace );
    manager.getAnimationSystem().evaluate( 0.5f );
    const SkeletalAnimatorComponent* pAnimator = pUnit->getOwner()->getComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pAnimator );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pAnimator->getCurveValue( hashed_string( "Happy" ) ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f + pAnimator->getCurveValue( hashed_string( "smile" ) ), TestFacialAnimationInternal::getWeight( *pUnit, "smile" ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.15f + pAnimator->getCurveValue( hashed_string( "mouthWide" ) ), TestFacialAnimationInternal::getWeight( *pUnit, "mouthWide" ), 0.0001f );
}
