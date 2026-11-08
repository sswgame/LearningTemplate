#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Audio/AudioSnapshotMixer.h"
#include "Engine/Audio/AudioTypes.h"

using namespace sw;

namespace
{
    struct AudioSnapshotTestUtil
    {
        static constexpr float32 kFadeSeconds = audio::kBlockSeconds * 2.0f; // 블록 둘에 끝나는 페이드

        static AudioSnapshotDef makeSnapshot( const utf8* pName, float32 musicDb )
        {
            AudioSnapshotDef snapshot{};
            snapshot._name           = hashed_string( pName );
            snapshot._fadeInSeconds  = kFadeSeconds;
            snapshot._fadeOutSeconds = kFadeSeconds;
            AudioSnapshotBusDef busVolume{};
            busVolume._bus      = hashed_string( "music" );
            busVolume._volumeDb = musicDb;
            snapshot._listBusVolume.push_back( busVolume );
            return snapshot;
        }

        static AudioMixerDef makeDesc()
        {
            AudioMixerDef desc{};
            AudioBusDef   master{};
            master._name = hashed_string( AudioBusNames::kMaster );
            desc._listBus.push_back( master );
            AudioBusDef music{};
            music._name   = hashed_string( "music" );
            music._parent = master._name;
            desc._listBus.push_back( music );
            desc._listSnapshot.push_back( makeSnapshot( "Duck", -12.0f ) );
            desc._listSnapshot.push_back( makeSnapshot( "Hush", -6.0f ) );
            return desc;
        }

        static float32 getMusicOffsetDb( const AudioMixer& mixer )
        {
            return mixer.getBusVolumeOffsetDb( mixer.findBusIndex( hashed_string( "music" ) ).value() );
        }
    };
    using Util = AudioSnapshotTestUtil;
} // namespace

/** @brief [AudioSnapshotMixerTest] 세기는 페이드 시간에 맞춰 오르내리고 오프셋은 세기 x dB 다. 다 빠지면 목록에서 사라진다 */
SW_TEST_CASE( AudioSnapshotMixerTest, FadesInAndOut )
{
    AudioMixer mixer;
    SW_REQUIRE_TRUE( mixer.initialize( Util::makeDesc(), AudioChannelLayout::Stereo ) );
    AudioSnapshotMixer  snapshots;
    const hashed_string duck( "Duck" );

    snapshots.startSnapshot( mixer, duck );
    SW_EXPECT_NEAR( 0.0f, snapshots.getSnapshotIntensity( duck ), 0.0001f );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( 0.5f, snapshots.getSnapshotIntensity( duck ), 0.0001f );
    SW_EXPECT_NEAR( -6.0f, Util::getMusicOffsetDb( mixer ), 0.001f );
    snapshots.update( mixer );
    snapshots.update( mixer ); // 목표에서 멈춘다
    SW_EXPECT_NEAR( 1.0f, snapshots.getSnapshotIntensity( duck ), 0.0001f );
    SW_EXPECT_NEAR( -12.0f, Util::getMusicOffsetDb( mixer ), 0.001f );

    snapshots.stopSnapshot( duck );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( 0.5f, snapshots.getSnapshotIntensity( duck ), 0.0001f );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( 0.0f, Util::getMusicOffsetDb( mixer ), 0.001f );
    SW_EXPECT_NEAR( 0.0f, snapshots.getSnapshotIntensity( duck ), 0.0001f );
}

/** @brief [AudioSnapshotMixerTest] 페이드 시간이 0 이면 바로 닿는다 — 0 으로 나누지 않는다 */
SW_TEST_CASE( AudioSnapshotMixerTest, ZeroFadeIsInstant )
{
    AudioMixerDef desc                   = Util::makeDesc();
    desc._listSnapshot[0]._fadeInSeconds = 0.0f;
    AudioMixer mixer;
    SW_REQUIRE_TRUE( mixer.initialize( desc, AudioChannelLayout::Stereo ) );
    AudioSnapshotMixer snapshots;
    snapshots.startSnapshot( mixer, hashed_string( "Duck" ) );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( 1.0f, snapshots.getSnapshotIntensity( hashed_string( "Duck" ) ), 0.0001f );
}

/** @brief [AudioSnapshotMixerTest] 켠 스냅샷들의 버스 오프셋은 더해진다 */
SW_TEST_CASE( AudioSnapshotMixerTest, BusOffsetsAdd )
{
    AudioMixer mixer;
    SW_REQUIRE_TRUE( mixer.initialize( Util::makeDesc(), AudioChannelLayout::Stereo ) );
    AudioSnapshotMixer snapshots;
    snapshots.setSnapshotIntensity( mixer, hashed_string( "Duck" ), 1.0f );
    snapshots.setSnapshotIntensity( mixer, hashed_string( "Hush" ), 0.5f );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( -15.0f, Util::getMusicOffsetDb( mixer ), 0.001f );
}

/** @brief [AudioSnapshotMixerTest] 게임이 끄는 세기는 페이드가 없고 0 이어도 목록에 남는다. 범위 밖 값은 0 ~ 1 로 자른다 */
SW_TEST_CASE( AudioSnapshotMixerTest, DrivenIntensityIsClampedAndStays )
{
    AudioMixer mixer;
    SW_REQUIRE_TRUE( mixer.initialize( Util::makeDesc(), AudioChannelLayout::Stereo ) );
    AudioSnapshotMixer  snapshots;
    const hashed_string duck( "Duck" );

    snapshots.setSnapshotIntensity( mixer, duck, 3.0f );
    SW_EXPECT_NEAR( 1.0f, snapshots.getSnapshotIntensity( duck ), 0.0001f );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( -12.0f, Util::getMusicOffsetDb( mixer ), 0.001f );

    snapshots.setSnapshotIntensity( mixer, duck, -1.0f );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( 0.0f, snapshots.getSnapshotIntensity( duck ), 0.0001f );

    // 0 에서 다시 끌어도 페이드 없이 바로 그 값이다
    snapshots.update( mixer );
    snapshots.setSnapshotIntensity( mixer, duck, 0.25f );
    SW_EXPECT_NEAR( 0.25f, snapshots.getSnapshotIntensity( duck ), 0.0001f );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( -3.0f, Util::getMusicOffsetDb( mixer ), 0.001f );

    // 끄면 지금 세기에서 페이드로 내려간다
    snapshots.stopSnapshot( duck );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( 0.0f, snapshots.getSnapshotIntensity( duck ), 0.0001f );
}

/** @brief [AudioSnapshotMixerTest] 없는 스냅샷은 오류를 남기고 아무것도 켜지 않는다 */
SW_TEST_CASE( AudioSnapshotMixerTest, UnknownSnapshotIsAnError )
{
    SW_EXPECT_LOG( "AudioSnapshotMixer.UnknownAudioSnapshot" );
    AudioMixer mixer;
    SW_REQUIRE_TRUE( mixer.initialize( Util::makeDesc(), AudioChannelLayout::Stereo ) );
    AudioSnapshotMixer snapshots;
    snapshots.startSnapshot( mixer, hashed_string( "Nope" ) );
    snapshots.update( mixer );
    SW_EXPECT_NEAR( 0.0f, snapshots.getSnapshotIntensity( hashed_string( "Nope" ) ), 0.0001f );
    SW_EXPECT_NEAR( 0.0f, Util::getMusicOffsetDb( mixer ), 0.001f );
}

/** @brief [AudioSnapshotMixerTest] 사용자 볼륨은 0 ~ 1 로 자른다. 음소거는 데이터 값과 OR 다 — 사용자가 꺼도 데이터의 음소거는 남는다 */
SW_TEST_CASE( AudioSnapshotMixerTest, UserStateCombinesWithData )
{
    AudioMixerDef desc       = Util::makeDesc();
    desc._listBus[1]._bMuted = true;
    AudioMixer mixer;
    SW_REQUIRE_TRUE( mixer.initialize( desc, AudioChannelLayout::Stereo ) );
    AudioSnapshotMixer  snapshots;
    const hashed_string music( "music" );
    const uint32        musicIndex = mixer.findBusIndex( music ).value();

    snapshots.setBusUserVolume( mixer, music, 5.0f );
    SW_EXPECT_NEAR( 1.0f, mixer.getBusUserVolume( musicIndex ), 0.0001f );
    snapshots.setBusMuted( mixer, music, false );
    SW_EXPECT_TRUE( mixer.isBusMuted( musicIndex ) );
    snapshots.setBusSolo( mixer, music, true );
    SW_EXPECT_TRUE( mixer.isBusSolo( musicIndex ) );
}

/** @brief [AudioSnapshotMixerTest] 믹서를 갈아 끼우면 사용자 상태는 옮겨 가고(그때 생긴 버스 포함), 새 그래프에 없는 스냅샷은 지운다 */
SW_TEST_CASE( AudioSnapshotMixerTest, AdoptMixerKeepsUserStateAndDropsLostSnapshots )
{
    AudioMixer oldMixer;
    SW_REQUIRE_TRUE( oldMixer.initialize( Util::makeDesc(), AudioChannelLayout::Stereo ) );
    AudioSnapshotMixer  snapshots;
    const hashed_string music( "music" );
    snapshots.setBusUserVolume( oldMixer, music, 0.25f );
    snapshots.setBusUserVolume( oldMixer, hashed_string( "late" ), 0.75f ); // 아직 없는 버스의 값은 그 이름이 생길 때까지 들고 있다
    snapshots.setSnapshotIntensity( oldMixer, hashed_string( "Duck" ), 1.0f );
    snapshots.setSnapshotIntensity( oldMixer, hashed_string( "Hush" ), 1.0f );

    AudioMixerDef nextDesc = Util::makeDesc();
    nextDesc._listSnapshot.erase( nextDesc._listSnapshot.begin() ); // Duck 이 사라진다
    AudioBusDef late{};
    late._name   = hashed_string( "late" );
    late._parent = hashed_string( AudioBusNames::kMaster );
    nextDesc._listBus.push_back( late );
    AudioMixer nextMixer;
    SW_REQUIRE_TRUE( nextMixer.initialize( nextDesc, AudioChannelLayout::Stereo ) );

    snapshots.applyMixer( nextMixer );
    SW_EXPECT_NEAR( 0.25f, nextMixer.getBusUserVolume( nextMixer.findBusIndex( music ).value() ), 0.0001f );
    SW_EXPECT_NEAR( 0.75f, nextMixer.getBusUserVolume( nextMixer.findBusIndex( hashed_string( "late" ) ).value() ), 0.0001f );
    SW_EXPECT_NEAR( 0.0f, snapshots.getSnapshotIntensity( hashed_string( "Duck" ) ), 0.0001f );
    SW_EXPECT_NEAR( 1.0f, snapshots.getSnapshotIntensity( hashed_string( "Hush" ) ), 0.0001f );
    snapshots.update( nextMixer );
    SW_EXPECT_NEAR( -6.0f, Util::getMusicOffsetDb( nextMixer ), 0.001f );
}
