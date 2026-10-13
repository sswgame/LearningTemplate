#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/FunctionSubsystem.h"
#include "Engine/Common/SubsystemCollection.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 서브시스템이 불린 순서를 적습니다 — "I:" 초기화, "S:" 종료, "D:" 소멸. */
    struct SubsystemRecorder
    {
        vector<string>    _listEvent{};
        vector<MemoryTag> _listInitializeMemoryTag{}; ///< 초기화가 불린 동안의 할당 태그(초기화한 순서)
        string            _failName{};                ///< 이 이름의 초기화가 `_failResult` 를 돌려준다
        EngineInitResult  _failResult{ EngineInitResult::Succeeded };

        /** @brief @p kind 로 시작하는 사건의 이름들을 공백으로 이어 붙입니다. */
        string joinEvents( utf8 kind ) const
        {
            StringBuilder<constant::kMaxBuffer512> sb;
            for ( const string& event : _listEvent )
            {
                if ( event[0] != kind )
                    continue;
                if ( sb.size() > 0 )
                    sb.append( ' ' );
                sb.append( string_view{ event }.substr( 2 ) );
            }
            return string( sb.c_str() );
        }
    };

    /** @brief 이름 · 의존 · 대상 · 태그를 받아 불린 순서를 적는 서브시스템입니다. 소멸자도 적는다(소멸 순서를 본다). */
    class RecordingSubsystem final : public ISubsystem
    {
    public:
        RecordingSubsystem( SubsystemRecorder& recorder, const utf8* pName, const utf8* pDependencies, EngineInitTarget target = EngineInitTarget::All,
                            MemoryTag memoryTag = MemoryTag::EngineMisc )
            : _recorder{ recorder }
            , _pName{ pName }
            , _pDependencies{ pDependencies }
            , _target{ target }
            , _memoryTag{ memoryTag }
        {
        }
        ~RecordingSubsystem() override { _recorder._listEvent.push_back( string( "D:" ) + _pName ); }

        EngineInitResult initializeSubsystem() override
        {
            _recorder._listEvent.push_back( string( "I:" ) + _pName );
            _recorder._listInitializeMemoryTag.push_back( Memory::getCurrentMemoryTag() );
            return _recorder._failName == _pName ? _recorder._failResult : EngineInitResult::Succeeded;
        }
        void shutdownSubsystem() override { _recorder._listEvent.push_back( string( "S:" ) + _pName ); }

        const utf8*      getSubsystemName() const override { return _pName; }
        const utf8*      getSubsystemDependencies() const override { return _pDependencies; }
        EngineInitTarget getSubsystemTarget() const override { return _target; }
        MemoryTag        getSubsystemMemoryTag() const override { return _memoryTag; }

    private:
        SubsystemRecorder& _recorder;
        const utf8*        _pName;
        const utf8*        _pDependencies;
        EngineInitTarget   _target;
        MemoryTag          _memoryTag;
    };

    /**
     * @brief 렌더러 모양의 그래프입니다(추가 순서는 이름과 어긋나게 둔다).
     *        Config ← Resource ← Scene, Config ← Device ← Renderer ← RenderThread, Device · Scene ← SceneDevice, Tools(의존 없음).
     *        Device 와 그에 기대는 셋은 클라이언트 전용이다.
     */
    void addRendererShapedGraph( SubsystemCollection& collection, SubsystemRecorder& recorder )
    {
        collection.add( make_unique<RecordingSubsystem>( recorder, "SceneDevice", "Device, Scene", EngineInitTarget::Client ) );
        collection.add( make_unique<RecordingSubsystem>( recorder, "RenderThread", "{ Renderer }", EngineInitTarget::Client ) );
        collection.add( make_unique<RecordingSubsystem>( recorder, "Renderer", "Device", EngineInitTarget::Client ) );
        collection.add( make_unique<RecordingSubsystem>( recorder, "Device", "Config", EngineInitTarget::Client, MemoryTag::RenderCPU ) );
        collection.add( make_unique<RecordingSubsystem>( recorder, "Scene", "Resource", EngineInitTarget::All, MemoryTag::Scene ) );
        collection.add( make_unique<RecordingSubsystem>( recorder, "Resource", "Config", EngineInitTarget::All, MemoryTag::Asset ) );
        collection.add( make_unique<RecordingSubsystem>( recorder, "Tools", "" ) );
        collection.add( make_unique<RecordingSubsystem>( recorder, "Config", "" ) );
    }

    /** @brief `addRendererShapedGraph` 의 초기화 순서입니다(의존이 앞, 동점은 이름 순). */
    constexpr const utf8* kRendererShapedOrder = "Config Device Renderer RenderThread Resource Scene SceneDevice Tools";

    /** @brief 이름 목록을 거꾸로 이어 붙입니다. */
    string reverseNames( string_view names )
    {
        vector<string_view> listName;
        size_t              cursor = 0;
        while ( cursor <= names.size() )
        {
            const size_t end = names.find( ' ', cursor );
            listName.push_back( names.substr( cursor, end == string_view::npos ? string_view::npos : end - cursor ) );
            if ( end == string_view::npos )
                break;
            cursor = end + 1;
        }
        StringBuilder<constant::kMaxBuffer512> sb;
        for ( size_t index = listName.size(); index > 0; --index )
        {
            if ( sb.size() > 0 )
                sb.append( ' ' );
            sb.append( listName[index - 1] );
        }
        return string( sb.c_str() );
    }

    /** @brief `FunctionSubsystem` 의 델리게이트 대상입니다. */
    struct FunctionTarget
    {
        EngineInitResult initialize()
        {
            ++_initializeCount;
            return EngineInitResult::Succeeded;
        }
        void shutdown() { ++_shutdownCount; }

        uint32 _initializeCount{ 0 };
        uint32 _shutdownCount{ 0 };
    };

    /** @brief 소멸을 적는 객체입니다(`FunctionSubsystem::takeOwnership` 의 소멸 자리를 본다). */
    struct OwnedProbe
    {
        explicit OwnedProbe( SubsystemRecorder& recorder )
            : _recorder{ recorder }
        {
        }
        ~OwnedProbe() { _recorder._listEvent.push_back( string( "D:Probe" ) ); }

        SubsystemRecorder& _recorder;
    };
} // namespace

/**
 * @brief [SubsystemCollectionTest] 순서는 의존이 정하고, 의존이 없는 것끼리는 이름 순이다(추가 순서는 보지 않는다)
 * @details 의존 글은 표의 꼴(`{ A, B }`)과 쉼표 · 공백 꼴을 섞는다. 같은 그래프를 다시 만들어도 같은 순서다.
 */
SW_TEST_CASE( SubsystemCollectionTest, DependenciesOrderAndNameBreaksTies )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE_MSG( collection.sort(), collection.getError().c_str() );
    SW_ASSERT_TRUE( collection.initializeAll( EngineInitTarget::Client ) );
    SW_EXPECT_STREQ( kRendererShapedOrder, recorder.joinEvents( 'I' ).c_str() );

    const vector<EngineInitNode> listNode = {
        {"D", "{ A, B }"},
        {"C",         ""},
        {"B",       "{}"},
        {"A",      " C "}
    };
    EngineInitGraph graph{};
    string          error;
    SW_ASSERT_TRUE_MSG( SubsystemCollection::computeGraph( listNode, graph, error ), error.c_str() );
    StringBuilder<constant::kMaxBuffer256> sb;
    for ( const uint32 nodeIndex : graph._listOrder )
    {
        if ( sb.size() > 0 )
            sb.append( ' ' );
        sb.append( listNode[nodeIndex]._pName );
    }
    SW_EXPECT_STREQ( "B C A D", sb.c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 종료는 초기화의 정확한 역순이고, 두 번 불러도 다시 내리지 않는다
 */
SW_TEST_CASE( SubsystemCollectionTest, ShutdownRunsInReverseOfInitialization )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE( collection.initializeAll( EngineInitTarget::Client ) );
    collection.shutdownAll();
    collection.shutdownAll();
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'S' ).c_str() );
    SW_EXPECT_TRUE( collection.getInitializedIndices().empty() );
}

/**
 * @brief [SubsystemCollectionTest] 실패하면 거기서 멈추고 초기화한 것만 역순으로 내리며, 소멸은 실패한 것 · 닿지 못한 것까지 모두 역순이다
 */
SW_TEST_CASE( SubsystemCollectionTest, FailureStopsAndShutsDownOnlyInitialized )
{
    SubsystemRecorder recorder;
    recorder._failName   = "Renderer";
    recorder._failResult = EngineInitResult::Failed;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_EXPECT_FALSE( collection.initializeAll( EngineInitTarget::Client ) );
    SW_EXPECT_STREQ( "Config Device Renderer", recorder.joinEvents( 'I' ).c_str() );

    collection.destroyAll();
    SW_EXPECT_STREQ( "Device Config", recorder.joinEvents( 'S' ).c_str() );
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'D' ).c_str() );
    SW_EXPECT_EQUAL( 0u, collection.getSubsystemCount() );
}

/**
 * @brief [SubsystemCollectionTest] `SkipDependents` 는 그것에 (간접으로라도) 의존하는 것만 건너뛰고, 건너뛴 것은 종료를 받지 않는다
 */
SW_TEST_CASE( SubsystemCollectionTest, SkipDependentsSkipsEveryDependent )
{
    SubsystemRecorder recorder;
    recorder._failName   = "Device";
    recorder._failResult = EngineInitResult::SkipDependents;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE( collection.initializeAll( EngineInitTarget::Client ) );
    SW_EXPECT_STREQ( "Config Device Resource Scene Tools", recorder.joinEvents( 'I' ).c_str() );
    collection.shutdownAll();
    SW_EXPECT_STREQ( "Tools Scene Resource Device Config", recorder.joinEvents( 'S' ).c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 소멸은 모든 종료가 끝난 **뒤에** 모두를 초기화 순서의 역순으로 지운다(소멸자도 같다)
 */
SW_TEST_CASE( SubsystemCollectionTest, DestroyRunsAfterEveryShutdownInReverseOrder )
{
    SubsystemRecorder recorder;
    {
        SubsystemCollection collection;
        addRendererShapedGraph( collection, recorder );
        SW_ASSERT_TRUE( collection.initializeAll( EngineInitTarget::Client ) );
    }
    const size_t count = 8;
    SW_ASSERT_EQUAL( count * 3, recorder._listEvent.size() );
    for ( size_t eventIndex = 0; eventIndex < recorder._listEvent.size(); ++eventIndex )
    {
        const utf8 expectedKind = ( eventIndex < count ) ? 'I' : ( eventIndex < count * 2 ) ? 'S'
                                                                                            : 'D';
        SW_EXPECT_TRUE_MSG( recorder._listEvent[eventIndex][0] == expectedKind, recorder._listEvent[eventIndex].c_str() );
    }
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'D' ).c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 초기화하지 않은 목록의 소멸은 종료를 부르지 않고 모두를 의존의 역순으로 지운다
 */
SW_TEST_CASE( SubsystemCollectionTest, DestroyWithoutInitializeOnlyDestroys )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE( collection.sort() );
    collection.destroyAll();
    SW_EXPECT_STREQ( "", recorder.joinEvents( 'S' ).c_str() );
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'D' ).c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 순환 · 모르는 의존 · 같은 이름은 정렬 오류이고 기동하지 않는다(소멸은 추가의 역순으로라도 한다)
 */
SW_TEST_CASE( SubsystemCollectionTest, InvalidGraphIsRejected )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    collection.add( make_unique<RecordingSubsystem>( recorder, "A", "C" ) );
    collection.add( make_unique<RecordingSubsystem>( recorder, "B", "A" ) );
    collection.add( make_unique<RecordingSubsystem>( recorder, "C", "B" ) );
    collection.add( make_unique<RecordingSubsystem>( recorder, "D", "" ) );
    SW_EXPECT_FALSE( collection.sort() );
    SW_EXPECT_STREQ( "Startup steps in or behind a dependency cycle: A B C", collection.getError().c_str() );
    SW_EXPECT_FALSE( collection.initializeAll( EngineInitTarget::Client ) );
    SW_EXPECT_TRUE( recorder._listEvent.empty() );
    collection.destroyAll();
    SW_EXPECT_STREQ( "D C B A", recorder.joinEvents( 'D' ).c_str() );

    EngineInitGraph graph{};
    string          error;
    SW_EXPECT_FALSE( SubsystemCollection::computeGraph( {
                                                            { "A", "Missing" }
    },
                                                        graph, error ) );
    SW_EXPECT_STREQ( "Startup step 'A' depends on unknown step 'Missing'", error.c_str() );
    error.clear();
    SW_EXPECT_FALSE( SubsystemCollection::computeGraph( {
                                                            {"A", ""},
                                                            {"A", ""}
    },
                                                        graph, error ) );
    SW_EXPECT_STREQ( "Startup step 'A' is declared twice", error.c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 서버에서 도는 것이 클라이언트 전용에 의존하면 정렬 오류다(대상 어긋남)
 */
SW_TEST_CASE( SubsystemCollectionTest, TargetMismatchIsRejected )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    collection.add( make_unique<RecordingSubsystem>( recorder, "Device", "", EngineInitTarget::Client ) );
    collection.add( make_unique<RecordingSubsystem>( recorder, "Scene", "Device", EngineInitTarget::All ) );
    SW_EXPECT_FALSE( collection.sort() );
    SW_EXPECT_STREQ( "Startup step 'Scene' runs on a target where its dependency 'Device' does not", collection.getError().c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 호스트 대상과 겹치지 않는 것은 초기화 · 종료하지 않고, 소멸은 모두 받는다
 */
SW_TEST_CASE( SubsystemCollectionTest, ServerTargetSkipsClientSubsystems )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE_MSG( collection.sort(), collection.getError().c_str() );
    SW_ASSERT_TRUE( collection.initializeAll( EngineInitTarget::Server ) );
    SW_EXPECT_STREQ( "Config Resource Scene Tools", recorder.joinEvents( 'I' ).c_str() );
    collection.destroyAll();
    SW_EXPECT_STREQ( "Tools Scene Resource Config", recorder.joinEvents( 'S' ).c_str() );
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'D' ).c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 하나에 (간접으로라도) 의존하는 것만 역순으로 내렸다가 같은 본문으로 다시 세우고, 그 뒤 종료 순서는 처음과 같다
 */
SW_TEST_CASE( SubsystemCollectionTest, DependentsRestartWithTheSameBodies )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE( collection.initializeAll( EngineInitTarget::Client ) );
    const ISubsystem* pDevice = nullptr;
    for ( uint32 index = 0; index < collection.getSubsystemCount(); ++index )
    {
        if ( string_view{ collection.getSubsystem( index ).getSubsystemName() } == "Device" )
            pDevice = &collection.getSubsystem( index );
    }
    SW_ASSERT_TRUE( pDevice != nullptr );
    recorder._listEvent.clear();

    collection.shutdownDependentsOf( *pDevice );
    SW_EXPECT_STREQ( "SceneDevice RenderThread Renderer", recorder.joinEvents( 'S' ).c_str() );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( collection.getStoppedIndices().size() ) );

    SW_EXPECT_TRUE( collection.restartStopped() );
    SW_EXPECT_STREQ( "Renderer RenderThread SceneDevice", recorder.joinEvents( 'I' ).c_str() );
    SW_EXPECT_TRUE( collection.getStoppedIndices().empty() );

    recorder._listEvent.clear();
    collection.shutdownAll();
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'S' ).c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 다시 세우다 실패하면 거기서 멈추고, 서지 못한 것은 종료 대상에서 빠진다
 */
SW_TEST_CASE( SubsystemCollectionTest, FailedRestartLeavesTheRestStopped )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE( collection.initializeAll( EngineInitTarget::Client ) );
    const ISubsystem* pDevice = nullptr;
    for ( uint32 index = 0; index < collection.getSubsystemCount(); ++index )
    {
        if ( string_view{ collection.getSubsystem( index ).getSubsystemName() } == "Device" )
            pDevice = &collection.getSubsystem( index );
    }
    SW_ASSERT_TRUE( pDevice != nullptr );
    collection.shutdownDependentsOf( *pDevice );
    recorder._failName   = "RenderThread";
    recorder._failResult = EngineInitResult::Failed;
    SW_EXPECT_FALSE( collection.restartStopped() );

    recorder._listEvent.clear();
    collection.shutdownAll();
    SW_EXPECT_STREQ( "Tools Scene Resource Renderer Device Config", recorder.joinEvents( 'S' ).c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 초기화(기동 · 재시작)는 서브시스템이 선언한 메모리 태그 아래에서 돈다
 */
SW_TEST_CASE( SubsystemCollectionTest, InitializeRunsUnderTheSubsystemMemoryTag )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );

    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE( collection.initializeAll( EngineInitTarget::Client ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 8 ), recorder._listInitializeMemoryTag.size() );
    // 순서: Config Device Renderer RenderThread Resource Scene SceneDevice Tools
    SW_EXPECT_TRUE( recorder._listInitializeMemoryTag[0] == MemoryTag::EngineMisc );
    SW_EXPECT_TRUE( recorder._listInitializeMemoryTag[1] == MemoryTag::RenderCPU );
    SW_EXPECT_TRUE( recorder._listInitializeMemoryTag[4] == MemoryTag::Asset );
    SW_EXPECT_TRUE( recorder._listInitializeMemoryTag[5] == MemoryTag::Scene );
    SW_EXPECT_TRUE( Memory::getCurrentMemoryTag() == MemoryTag::Unknown );
}

/**
 * @brief [SubsystemCollectionTest] `FunctionSubsystem` 은 묶은 델리게이트를 부르고, 묶지 않은 초기화는 성공 · 종료는 아무것도 하지 않는다
 */
SW_TEST_CASE( SubsystemCollectionTest, FunctionSubsystemCallsItsDelegates )
{
    FunctionTarget      target;
    SubsystemCollection collection;
    collection.add( make_unique<FunctionSubsystem>( FunctionSubsystemDesc{ "Bound", "Unbound", EngineInitTarget::All, MemoryTag::EngineMisc },
                                                    SW_DELEGATE_METHOD( SubsystemInitializeDelegate, &FunctionTarget::initialize, &target ),
                                                    SW_DELEGATE_METHOD( SubsystemShutdownDelegate, &FunctionTarget::shutdown, &target ) ) );
    collection.add( make_unique<FunctionSubsystem>( FunctionSubsystemDesc{ "Unbound", "", EngineInitTarget::All, MemoryTag::EngineMisc } ) );
    SW_ASSERT_TRUE_MSG( collection.initializeAll( EngineInitTarget::Client ), collection.getError().c_str() );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( collection.getInitializedIndices().size() ) );
    SW_EXPECT_EQUAL( 1u, target._initializeCount );
    collection.shutdownAll();
    SW_EXPECT_EQUAL( 1u, target._shutdownCount );
}

/**
 * @brief [SubsystemCollectionTest] `FunctionSubsystem` 이 넘겨받은 객체는 그 서브시스템의 소멸 자리(초기화의 역순)에서 함께 소멸한다
 * @details Core 서비스(태스크 · 코덱 레지스트리)의 소유를 기동 순서로 옮기는 근거다 — 그에 기대는 것이 모두 소멸한 뒤, 그것이 기대는 것보다 먼저다.
 */
SW_TEST_CASE( SubsystemCollectionTest, FunctionSubsystemDestroysWhatItOwnsInItsPlace )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    collection.add( make_unique<RecordingSubsystem>( recorder, "Later", "Owner" ) );
    FunctionSubsystem& owner = collection.add( make_unique<FunctionSubsystem>( FunctionSubsystemDesc{ "Owner", "Earlier", EngineInitTarget::All, MemoryTag::EngineMisc } ) );
    OwnedProbe&        probe = owner.takeOwnership( make_unique<OwnedProbe>( recorder ) );
    SW_EXPECT_TRUE( &probe._recorder == &recorder );
    collection.add( make_unique<RecordingSubsystem>( recorder, "Earlier", "" ) );
    SW_ASSERT_TRUE_MSG( collection.initializeAll( EngineInitTarget::Client ), collection.getError().c_str() );

    collection.destroyAll();
    SW_EXPECT_STREQ( "Later Probe Earlier", recorder.joinEvents( 'D' ).c_str() );
}
