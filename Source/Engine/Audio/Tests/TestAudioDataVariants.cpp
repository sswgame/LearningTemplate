#include "pch.h"

#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

using namespace sw;

namespace
{
    /** @brief 부모 글을 한 벌 돌려주는 곳입니다(`DataInheritance::flatten` 의 입력). */
    class AudioParentSource final : public IDataInheritanceSource
    {
    public:
        explicit AudioParentSource( string_view text )
            : _text{ text }
        {
        }
        [[nodiscard]] bool readText( string_view /*assetID*/, string& outText ) override
        {
            outText = _text;
            return true;
        }

    private:
        string _text;
    };

    struct AudioDataVariantTestUtil
    {
        static constexpr string_view kLibrary = R"(<AudioEventLibrary>
            <_listParameter><AudioParameterDef _name="Speed" _maxValue="10" /></_listParameter>
            <_listEvent>
                <AudioEventDef _name="Step" _bus="sfx" _pitchMax="2" _priority="40">
                    <_listClip><AudioClipEntry _path="a.ogg" /></_listClip>
                </AudioEventDef>
                <AudioEventDef _name="Fire" _bus="sfx"><_listClip><AudioClipEntry _path="b.ogg" /></_listClip></AudioEventDef>
            </_listEvent>
        </AudioEventLibrary>)";
    };
    using Util = AudioDataVariantTestUtil;
} // namespace

/** @brief [AudioDataVariantTest] 라이브러리 변형은 이벤트를 이름으로 합친다 — 적은 칸만 바뀌고, 클립은 물려받으며, _op="Remove" 는 지우고 새 이름은 더한다 */
SW_TEST_CASE( AudioDataVariantTest, LibraryVariantMergesEventsByName )
{
    AudioParentSource source{ Util::kLibrary };
    const string_view child = R"(<AudioEventLibrary _base="parent.audioevents.xml"><_listEvent>
        <AudioEventDef _name="Step" _pitchMax="5" />
        <AudioEventDef _name="Fire" _op="Remove" />
        <AudioEventDef _name="New" _bus="ui"><_listClip><AudioClipEntry _path="c.ogg" /></_listClip></AudioEventDef>
    </_listEvent></AudioEventLibrary>)";
    string            flat;
    SW_REQUIRE_TRUE( DataInheritance::flatten( child, "child", DataInheritanceSchema::makeFromType( *AudioEventLibrary::StaticType() ), source, flat ) );

    AudioEventLibrary library;
    SW_REQUIRE_TRUE( library.loadFromXMLText( flat ) );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( library._listEvent.size() ) );
    const AudioEventDef* pStep = library.findEvent( hashed_string( "Step" ) );
    SW_REQUIRE_NOT_NULL( pStep );
    SW_EXPECT_NEAR( 5.0f, pStep->_pitchMax, 0.0f );
    SW_EXPECT_EQUAL( 40, pStep->_priority );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pStep->_listClip.size() ) );
    SW_EXPECT_TRUE( library.findEvent( hashed_string( "Fire" ) ) == nullptr );
    SW_EXPECT_TRUE( library.findEvent( hashed_string( "New" ) ) != nullptr );
}

/** @brief [AudioDataVariantTest] 프리셋은 같은 라이브러리의 이벤트를 부모로 삼는다 — 뒤에 있는 부모도 찾고, 사슬도 된다 */
SW_TEST_CASE( AudioDataVariantTest, PresetInheritsAnotherEvent )
{
    AudioEventLibrary library;
    SW_REQUIRE_TRUE( library.loadFromXMLText( R"(<AudioEventLibrary><_listEvent>
        <AudioEventDef _name="Heavy" _parentEvent="Step" _pitchMax="1" />
        <AudioEventDef _name="Heaviest" _parentEvent="Heavy" _priority="90" />
        <AudioEventDef _name="Step" _bus="sfx" _pitchMax="2" _priority="40"><_listClip><AudioClipEntry _path="a.ogg" /></_listClip></AudioEventDef>
    </_listEvent></AudioEventLibrary>)" ) );
    const AudioEventDef* pHeaviest = library.findEvent( hashed_string( "Heaviest" ) );
    SW_REQUIRE_NOT_NULL( pHeaviest );
    SW_EXPECT_EQUAL( 90, pHeaviest->_priority );
    SW_EXPECT_NEAR( 1.0f, pHeaviest->_pitchMax, 0.0f );
    SW_EXPECT_TRUE( pHeaviest->_bus == hashed_string( "sfx" ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pHeaviest->_listClip.size() ) );
}

/** @brief [AudioDataVariantTest] 없는 부모와 순환은 읽기 실패다 — 부모 없이 자식 칸만으로 읽지 않는다 */
SW_TEST_CASE( AudioDataVariantTest, PresetErrors )
{
    SW_EXPECT_LOG( "DataInheritance.BaseNotFound" );
    SW_EXPECT_LOG( "DataInheritance.Cycle" );
    AudioEventLibrary library;
    SW_EXPECT_FALSE( library.loadFromXMLText( R"(<AudioEventLibrary><_listEvent><AudioEventDef _name="A" _parentEvent="Missing" /></_listEvent></AudioEventLibrary>)" ) );
    SW_EXPECT_FALSE( library.loadFromXMLText(
        R"(<AudioEventLibrary><_listEvent><AudioEventDef _name="A" _parentEvent="B" /><AudioEventDef _name="B" _parentEvent="A" /></_listEvent></AudioEventLibrary>)" ) );
}

/** @brief [AudioDataVariantTest] 믹서 변형은 버스 · 센드를 이름으로 합치고 검사는 합친 결과에 걸린다 */
SW_TEST_CASE( AudioDataVariantTest, MixerVariantMergesBusesByName )
{
    AudioParentSource source{ R"(<AudioMixerDef _maxVoiceCount="32"><_listBus>
        <AudioBusDef _name="master" />
        <AudioBusDef _name="reverb" _parent="master" />
        <AudioBusDef _name="sfx" _parent="master" _volumeDb="-3"><_listSend><AudioSendDef _bus="reverb" _levelDb="-14" /></_listSend></AudioBusDef>
    </_listBus></AudioMixerDef>)" };
    const string_view child = R"(<AudioMixerDef _base="parent.audiomixer.xml" _maxVoiceCount="16"><_listBus>
        <AudioBusDef _name="sfx" _volumeDb="-6"><_listSend><AudioSendDef _bus="reverb" _levelDb="-6" /></_listSend></AudioBusDef>
        <AudioBusDef _name="ui" _parent="master" />
    </_listBus></AudioMixerDef>)";
    string            flat;
    SW_REQUIRE_TRUE( DataInheritance::flatten( child, "child", DataInheritanceSchema::makeFromType( *AudioMixerDef::StaticType() ), source, flat ) );

    AudioMixerDef mixer;
    SW_REQUIRE_TRUE( mixer.loadFromXMLText( flat ) );
    SW_EXPECT_EQUAL( 16u, mixer._maxVoiceCount );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( mixer._listBus.size() ) );
    const AudioBusDef& sfx = mixer._listBus[static_cast<size_t>( mixer.findBusIndex( hashed_string( "sfx" ) ) )];
    SW_EXPECT_NEAR( -6.0f, sfx._volumeDb, 0.0f );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( sfx._listSend.size() ) );
    SW_EXPECT_NEAR( -6.0f, sfx._listSend[0]._levelDb, 0.0f );
}

/** @brief [AudioDataVariantTest] 변형의 차이 글을 부모 위에 펼치면 고친 값이 나온다 — `saveToAsset` 이 변형을 변형으로 쓰는 근거다 */
SW_TEST_CASE( AudioDataVariantTest, DeltaRoundTrips )
{
    AudioEventLibrary parent;
    SW_REQUIRE_TRUE( parent.loadFromXMLText( Util::kLibrary ) );
    AudioEventLibrary edited                               = parent;
    edited.findEvent( hashed_string( "Step" ) )->_pitchMax = 7.0f;

    const DataInheritanceSchema schema = DataInheritanceSchema::makeFromType( *AudioEventLibrary::StaticType() );
    string                      delta;
    SW_REQUIRE_TRUE( DataInheritance::makeDelta( edited.saveToXMLText(), parent.saveToXMLText(), schema, "parent.audioevents.xml", delta ) );
    SW_EXPECT_TRUE( delta.find( "_base=\"parent.audioevents.xml\"" ) != string::npos );
    SW_EXPECT_TRUE( delta.find( "Fire" ) == string::npos );

    AudioParentSource source{ parent.saveToXMLText() };
    string            flat;
    SW_REQUIRE_TRUE( DataInheritance::flatten( delta, "child", schema, source, flat ) );
    AudioEventLibrary reloaded;
    SW_REQUIRE_TRUE( reloaded.loadFromXMLText( flat ) );
    const AudioEventDef* pStep = reloaded.findEvent( hashed_string( "Step" ) );
    SW_REQUIRE_NOT_NULL( pStep );
    SW_EXPECT_NEAR( 7.0f, pStep->_pitchMax, 0.0f );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( reloaded._listEvent.size() ) );
}

/** @brief [AudioDataVariantTest] 프리셋은 부모 이벤트와의 차이로 저장된다 — 저장 → 다시 읽기가 같은 값이고, 부모와 같은 칸은 적지 않아 부모를 고치면 따라간다 */
SW_TEST_CASE( AudioDataVariantTest, PresetSavesOnlyItsDifference )
{
    AudioEventLibrary library;
    SW_REQUIRE_TRUE( library.loadFromXMLText( R"(<AudioEventLibrary><_listEvent>
        <AudioEventDef _name="Heavy" _parentEvent="Step" _pitchMax="1" />
        <AudioEventDef _name="Step" _bus="sfx" _pitchMax="2" _priority="40"><_listClip><AudioClipEntry _path="a.ogg" /></_listClip></AudioEventDef>
    </_listEvent></AudioEventLibrary>)" ) );
    AudioEventDef* pHeavy = library.findEvent( hashed_string( "Heavy" ) );
    SW_REQUIRE_NOT_NULL( pHeavy );
    SW_EXPECT_TRUE( pHeavy->_parentEvent == hashed_string( "Step" ) ); // 읽기가 부모 표시를 남긴다
    pHeavy->_pitchMax = 3.0f;

    const string saved = library.saveToXMLText();
    SW_REQUIRE_FALSE( saved.empty() );
    SW_EXPECT_TRUE( saved.find( "_parentEvent=\"Step\"" ) != string::npos );
    SW_EXPECT_EQUAL( saved.find( "_priority=\"40\"" ), saved.rfind( "_priority=\"40\"" ) ); // 부모의 것 한 번 — 프리셋은 물려받은 칸을 적지 않는다
    SW_EXPECT_EQUAL( saved.find( "a.ogg" ), saved.rfind( "a.ogg" ) );

    AudioEventLibrary reloaded;
    SW_REQUIRE_TRUE( reloaded.loadFromXMLText( saved ) );
    const AudioEventDef* pReloaded = reloaded.findEvent( hashed_string( "Heavy" ) );
    SW_REQUIRE_NOT_NULL( pReloaded );
    SW_EXPECT_NEAR( 3.0f, pReloaded->_pitchMax, 0.0f );
    SW_EXPECT_EQUAL( 40, pReloaded->_priority );
    SW_EXPECT_TRUE( pReloaded->_bus == hashed_string( "sfx" ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pReloaded->_listClip.size() ) );
    SW_EXPECT_TRUE( reloaded.saveToXMLText() == saved );
}

/** @brief [AudioDataVariantTest] 프리셋 부모가 라이브러리에 없으면 검사가 거르고, 저장 글은 비어 쓰지 않는다 */
SW_TEST_CASE( AudioDataVariantTest, PresetWithMissingParentIsNotSaved )
{
    SW_EXPECT_LOG( "AudioEventLibrary.EventPresetParentInvalid" );
    SW_EXPECT_LOG( "AudioEventLibrary.PresetParentMissing" );
    AudioEventLibrary library;
    SW_REQUIRE_TRUE( library.loadFromXMLText( Util::kLibrary ) );
    library.findEvent( hashed_string( "Fire" ) )->_parentEvent = hashed_string( "Missing" );
    SW_EXPECT_FALSE( library.validate( "edited" ) );
    SW_EXPECT_TRUE( library.saveToXMLText().empty() );
}

/** @brief [AudioDataVariantTest] 음악 변형은 구간을 이름으로 합친다 — 적은 칸만 바뀌고 레이어는 물려받으며 새 구간은 더한다 */
SW_TEST_CASE( AudioDataVariantTest, MusicVariantMergesSegmentsByName )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.music.xml" ), R"(<AudioMusicDef _bus="music" _startSegment="Explore">
        <_listSegment>
            <AudioMusicSegmentDef _name="Explore" _bars="8"><_listLayer><AudioMusicLayerDef _path="explore.ogg" /></_listLayer></AudioMusicSegmentDef>
            <AudioMusicSegmentDef _name="Combat" _bars="4"><_listLayer><AudioMusicLayerDef _path="combat.ogg" /></_listLayer></AudioMusicSegmentDef>
        </_listSegment>
    </AudioMusicDef>)" ) );
    const string  baseID = test::ScopedScratchDomain::makeID( "base.music.xml" ).str();
    AudioMusicDef music;
    SW_REQUIRE_TRUE( music.loadFromXMLText( "<AudioMusicDef _base=\"" + baseID + "\" _tempo=\"90\"><_listSegment><AudioMusicSegmentDef _name=\"Combat\" _bars=\"2\" />"
                                                                                 "<AudioMusicSegmentDef _name=\"Boss\"><_listLayer><AudioMusicLayerDef _path=\"boss.ogg\" /></_listLayer></AudioMusicSegmentDef></_listSegment></AudioMusicDef>",
                                            "child.music.xml" ) );
    SW_EXPECT_NEAR( 90.0f, music._tempo, 0.0f );
    SW_EXPECT_TRUE( music._bus == hashed_string( "music" ) );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( music._listSegment.size() ) );
    const AudioMusicSegmentDef& combat = music._listSegment[music.findSegmentIndex( hashed_string( "Combat" ) ).value()];
    SW_EXPECT_EQUAL( 2u, combat._bars );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( combat._listLayer.size() ) ); // 물려받음
    SW_EXPECT_TRUE( music.findSegmentIndex( hashed_string( "Boss" ) ).has_value() );
}

/** @brief [AudioDataVariantTest] 소리 그래프 변형은 노드를 이름으로 합치고, 저장은 부모와 다른 칸만 쓴다 — 저장 → 다시 읽기가 같은 그래프다 */
SW_TEST_CASE( AudioDataVariantTest, GraphVariantSavesOnlyItsDifference )
{
    if constexpr ( build::kWithClientCode == false )
    {
        // 전용 서버는 소리 그래프 파일을 읽지 않는다 — 이 변형 길이 돌지 않는 까닭을 확인만 한다.
        SW_EXPECT_TRUE( engine::getSubsystem<AssetPaths>().isExcludedForHost( "testproject/base.audiograph.xml" ) );
        return;
    }
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.audiograph.xml" ),
                                              R"(<AudioGraphDef _output="Osc" _durationSeconds="1"><_listNode><AudioGraphNodeDef _name="Osc" _type="Sine" /></_listNode></AudioGraphDef>)" ) );
    const string  baseID = test::ScopedScratchDomain::makeID( "base.audiograph.xml" ).str();
    AudioGraphDef graph;
    SW_REQUIRE_TRUE( graph.loadFromXMLText( "<AudioGraphDef _base=\"" + baseID + "\" _durationSeconds=\"2\" />", "child.audiograph.xml" ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( graph._listNode.size() ) );
    SW_EXPECT_EQUAL( baseID, graph._baseID );
    graph._durationSeconds = 3.0f;

    const optional<AbsolutePath> childPath = AbsolutePath::tryMake( domain.makeDiskPath( "child.audiograph.xml" ) );
    SW_REQUIRE_TRUE( childPath.has_value() );
    SW_REQUIRE_TRUE( graph.saveToAsset( *childPath ) );
    string saved;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( childPath->str(), saved ) );
    SW_EXPECT_TRUE( saved.find( baseID ) != string::npos );
    SW_EXPECT_TRUE( saved.find( "Sine" ) == string::npos ); // 물려받은 노드는 적지 않는다

    AudioGraphDef reloaded;
    SW_REQUIRE_TRUE( reloaded.loadFromAsset( test::ScopedScratchDomain::makeID( "child.audiograph.xml" ) ) );
    SW_EXPECT_NEAR( 3.0f, reloaded._durationSeconds, 0.0f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( reloaded._listNode.size() ) );
    SW_EXPECT_EQUAL( graph.saveToXMLText(), reloaded.saveToXMLText() );
}
