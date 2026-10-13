#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/SubsystemCollection.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 이 테스트의 호스트 종류 비트입니다(비트의 뜻은 쓰는 쪽이 정한다 — 엔진은 `EngineSubsystemHost`). */
    constexpr SubsystemHostMask kTestClient = static_cast<SubsystemHostMask>( 1 << 0 );
    constexpr SubsystemHostMask kTestServer = static_cast<SubsystemHostMask>( 1 << 1 );

    /** @brief 서브시스템이 불린 순서를 적습니다 — "I:" 초기화, "P:" 멈춤(stop), "S:" 종료, "D:" 소멸. */
    struct SubsystemRecorder
    {
        vector<string>      _listEvent{};
        vector<MemoryTag>   _listInitializeMemoryTag{}; ///< 초기화가 불린 동안의 할당 태그(초기화한 순서)
        string              _failName{};                ///< 이 이름의 초기화가 `_failResult` 를 돌려준다
        SubsystemInitResult _failResult{ SubsystemInitResult::Succeeded };

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
        RecordingSubsystem( SubsystemRecorder& recorder, const utf8* pName, vector<const utf8*> listDependency, SubsystemHostMask host = SubsystemHostMask::All,
                            MemoryTag memoryTag = MemoryTag::EngineMisc )
            : _recorder{ recorder }
            , _pName{ pName }
            , _listDependency{ std::move( listDependency ) }
            , _host{ host }
            , _memoryTag{ memoryTag }
        {
        }
        ~RecordingSubsystem() override { _recorder._listEvent.push_back( string( "D:" ) + _pName ); }

        SubsystemInitResult initializeSubsystem() override
        {
            _recorder._listEvent.push_back( string( "I:" ) + _pName );
            _recorder._listInitializeMemoryTag.push_back( Memory::getCurrentMemoryTag() );
            return _recorder._failName == _pName ? _recorder._failResult : SubsystemInitResult::Succeeded;
        }
        void stopSubsystem() override { _recorder._listEvent.push_back( string( "P:" ) + _pName ); }
        void shutdownSubsystem() override { _recorder._listEvent.push_back( string( "S:" ) + _pName ); }

        const utf8*             getSubsystemName() const override { return _pName; }
        span<const utf8* const> getSubsystemDependencies() const override { return _listDependency; }
        SubsystemHostMask       getSubsystemHost() const override { return _host; }
        MemoryTag               getSubsystemMemoryTag() const override { return _memoryTag; }

    private:
        SubsystemRecorder&  _recorder;
        const utf8*         _pName;
        vector<const utf8*> _listDependency;
        SubsystemHostMask   _host;
        MemoryTag           _memoryTag;
    };

    /**
     * @brief 렌더러 모양의 그래프입니다(추가 순서는 이름과 어긋나게 둔다).
     *        Config ← Resource ← Scene, Config ← Device ← Renderer ← RenderThread, Device · Scene ← SceneDevice, Tools(의존 없음).
     *        Device 와 그에 기대는 셋은 클라이언트 전용이다.
     */
    void addRendererShapedGraph( SubsystemCollection& collection, SubsystemRecorder& recorder )
    {
        collection.add( sw::make_unique<RecordingSubsystem>( recorder, "SceneDevice", vector<const utf8*>{ "Device", "Scene" }, kTestClient ) );
        collection.add( sw::make_unique<RecordingSubsystem>( recorder, "RenderThread", vector<const utf8*>{ "Renderer" }, kTestClient ) );
        collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Renderer", vector<const utf8*>{ "Device" }, kTestClient ) );
        collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Device", vector<const utf8*>{ "Config" }, kTestClient, MemoryTag::RenderCPU ) );
        collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Scene", vector<const utf8*>{ "Resource" }, SubsystemHostMask::All, MemoryTag::Scene ) );
        collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Resource", vector<const utf8*>{ "Config" }, SubsystemHostMask::All, MemoryTag::Asset ) );
        collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Tools", vector<const utf8*>{} ) );
        collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Config", vector<const utf8*>{} ) );
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
} // namespace

/**
 * @brief [SubsystemCollectionTest] 순서는 의존이 정하고, 의존이 없는 것끼리는 이름 순이다(추가 순서는 보지 않는다)
 * @details 같은 그래프를 다시 만들어도 같은 순서다.
 */
SW_TEST_CASE( SubsystemCollectionTest, DependenciesOrderAndNameBreaksTies )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE_MSG( collection.sort(), collection.getError().c_str() );
    SW_ASSERT_TRUE( collection.initializeAll( kTestClient ) );
    SW_EXPECT_STREQ( kRendererShapedOrder, recorder.joinEvents( 'I' ).c_str() );

    static constexpr const utf8* kArrDDependency[] = { "A", "B" };
    static constexpr const utf8* kArrADependency[] = { "C" };
    const vector<SubsystemNode>  listNode          = {
        {"D", kArrDDependency},
        {"C",              {}},
        {"B",              {}},
        {"A", kArrADependency}
    };
    SubsystemGraph graph{};
    string         error;
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
    SW_ASSERT_TRUE( collection.initializeAll( kTestClient ) );
    collection.shutdownAll();
    collection.shutdownAll();
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'P' ).c_str() );
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'S' ).c_str() );
    SW_EXPECT_TRUE( collection.getInitializedIndices().empty() );
}

/**
 * @brief [SubsystemCollectionTest] 종료 전 단계(stop)는 어느 종료보다도 먼저 모두에 역순으로 한 번씩 불린다(두 패스)
 * @details 한 서브시스템이 stop 에서 비우는 남은 일이, 그보다 뒤에 초기화된(먼저 내려갈) 서브시스템을 부를 수 있어야 한다 — 그래서 패스가 둘이다.
 */
SW_TEST_CASE( SubsystemCollectionTest, StopPassRunsBeforeEveryShutdown )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE( collection.initializeAll( kTestClient ) );
    recorder._listEvent.clear();
    collection.shutdownAll();

    const size_t count = 8;
    SW_ASSERT_EQUAL( count * 2, recorder._listEvent.size() );
    for ( size_t eventIndex = 0; eventIndex < recorder._listEvent.size(); ++eventIndex )
    {
        const utf8 expectedKind = eventIndex < count ? 'P' : 'S';
        SW_EXPECT_TRUE_MSG( recorder._listEvent[eventIndex][0] == expectedKind, recorder._listEvent[eventIndex].c_str() );
    }
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'P' ).c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 실패하면 거기서 멈추고 초기화한 것만 역순으로 내리며, 소멸은 실패한 것 · 닿지 못한 것까지 모두 역순이다
 */
SW_TEST_CASE( SubsystemCollectionTest, FailureStopsAndShutsDownOnlyInitialized )
{
    SubsystemRecorder recorder;
    recorder._failName   = "Renderer";
    recorder._failResult = SubsystemInitResult::Failed;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_EXPECT_FALSE( collection.initializeAll( kTestClient ) );
    SW_EXPECT_STREQ( "Config Device Renderer", recorder.joinEvents( 'I' ).c_str() );

    collection.destroyAll();
    SW_EXPECT_STREQ( "Device Config", recorder.joinEvents( 'P' ).c_str() );
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
    recorder._failResult = SubsystemInitResult::SkipDependents;
    SubsystemCollection collection;
    addRendererShapedGraph( collection, recorder );
    SW_ASSERT_TRUE( collection.initializeAll( kTestClient ) );
    SW_EXPECT_STREQ( "Config Device Resource Scene Tools", recorder.joinEvents( 'I' ).c_str() );
    collection.shutdownAll();
    SW_EXPECT_STREQ( "Tools Scene Resource Device Config", recorder.joinEvents( 'P' ).c_str() );
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
        SW_ASSERT_TRUE( collection.initializeAll( kTestClient ) );
    }
    const size_t count = 8;
    SW_ASSERT_EQUAL( count * 4, recorder._listEvent.size() );
    constexpr utf8 kKindByPass[] = { 'I', 'P', 'S', 'D' };
    for ( size_t eventIndex = 0; eventIndex < recorder._listEvent.size(); ++eventIndex )
    {
        const utf8 expectedKind = kKindByPass[eventIndex / count];
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
    SW_EXPECT_STREQ( "", recorder.joinEvents( 'P' ).c_str() );
    SW_EXPECT_STREQ( "", recorder.joinEvents( 'S' ).c_str() );
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'D' ).c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 역방향 의존(`getSubsystemDependents`)은 적은 이름을 자기 뒤에 세우고, 목록에 없는 이름은 건너뛴다
 * @details 낮은 층의 서브시스템은 높은 층의 이름 상수를 include 할 수 없다 — 높은 층 쪽이 "이것들은 내 뒤" 라고 적는다. 이름이 앞인 것이 먼저라는 동점 규칙을
 *          거슬러야 순서가 바뀐 것을 확인할 수 있으므로, 뒤에 서야 하는 쪽 이름을 앞 글자로 둔다.
 */
SW_TEST_CASE( SubsystemCollectionTest, DependentsInitializeAfterTheDeclarer )
{
    static constexpr const utf8* kArrDependent[] = { "Alpha", "NotRegistered" };
    SubsystemNode                later{ "Alpha", {} };
    SubsystemNode                declarer{ "Zulu", {} };
    declarer._listDependent = kArrDependent;
    SubsystemGraph graph{};
    string         error;
    SW_ASSERT_TRUE_MSG( SubsystemCollection::computeGraph( { later, declarer }, graph, error ), error.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), graph._listOrder.size() );
    SW_EXPECT_EQUAL( 1u, graph._listOrder[0] ); // Zulu 가 먼저
    SW_EXPECT_EQUAL( 0u, graph._listOrder[1] );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), graph._listDependency[0].size() );
    SW_EXPECT_EQUAL( 1u, graph._listDependency[0][0] );
}

/**
 * @brief [SubsystemCollectionTest] 순환 · 모르는 의존 · 같은 이름은 정렬 오류이고 기동하지 않는다(소멸은 추가의 역순으로라도 한다)
 */
SW_TEST_CASE( SubsystemCollectionTest, InvalidGraphIsRejected )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    collection.add( sw::make_unique<RecordingSubsystem>( recorder, "A", vector<const utf8*>{ "C" } ) );
    collection.add( sw::make_unique<RecordingSubsystem>( recorder, "B", vector<const utf8*>{ "A" } ) );
    collection.add( sw::make_unique<RecordingSubsystem>( recorder, "C", vector<const utf8*>{ "B" } ) );
    collection.add( sw::make_unique<RecordingSubsystem>( recorder, "D", vector<const utf8*>{} ) );
    SW_EXPECT_FALSE( collection.sort() );
    SW_EXPECT_STREQ( "Subsystems in or behind a dependency cycle: A B C", collection.getError().c_str() );
    SW_EXPECT_FALSE( collection.initializeAll( kTestClient ) );
    SW_EXPECT_TRUE( recorder._listEvent.empty() );
    collection.destroyAll();
    SW_EXPECT_STREQ( "D C B A", recorder.joinEvents( 'D' ).c_str() );

    SubsystemGraph               graph{};
    string                       error;
    static constexpr const utf8* kArrMissing[] = { "Missing" };
    SW_EXPECT_FALSE( SubsystemCollection::computeGraph( {
                                                            { "A", kArrMissing }
    },
                                                        graph, error ) );
    SW_EXPECT_STREQ( "Subsystem 'A' depends on unknown subsystem 'Missing'", error.c_str() );
    error.clear();
    SW_EXPECT_FALSE( SubsystemCollection::computeGraph( {
                                                            {"A", {}},
                                                            {"A", {}}
    },
                                                        graph, error ) );
    SW_EXPECT_STREQ( "Subsystem 'A' is declared twice", error.c_str() );
}

/**
 * @brief [SubsystemCollectionTest] 서버에서 도는 것이 클라이언트 전용에 의존하면 정렬 오류다(대상 어긋남)
 */
SW_TEST_CASE( SubsystemCollectionTest, TargetMismatchIsRejected )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Device", vector<const utf8*>{}, kTestClient ) );
    collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Scene", vector<const utf8*>{ "Device" }, SubsystemHostMask::All ) );
    SW_EXPECT_FALSE( collection.sort() );
    SW_EXPECT_STREQ( "Subsystem 'Scene' runs on a host where its dependency 'Device' does not", collection.getError().c_str() );
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
    SW_ASSERT_TRUE( collection.initializeAll( kTestServer ) );
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
    SW_ASSERT_TRUE( collection.initializeAll( kTestClient ) );
    const ISubsystem* pDevice = nullptr;
    for ( uint32 index = 0; index < collection.getSubsystemCount(); ++index )
    {
        if ( string_view{ collection.getSubsystem( index ).getSubsystemName() } == "Device" )
            pDevice = &collection.getSubsystem( index );
    }
    SW_ASSERT_TRUE( pDevice != nullptr );
    recorder._listEvent.clear();

    collection.shutdownDependentsOf( *pDevice );
    SW_EXPECT_STREQ( "SceneDevice RenderThread Renderer", recorder.joinEvents( 'P' ).c_str() );
    SW_EXPECT_STREQ( "SceneDevice RenderThread Renderer", recorder.joinEvents( 'S' ).c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 6 ), recorder._listEvent.size() );
    SW_EXPECT_TRUE( recorder._listEvent[2][0] == 'P' && recorder._listEvent[3][0] == 'S' ); // 모두 멈춘 뒤에 종료한다
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( collection.getStoppedIndices().size() ) );

    SW_EXPECT_TRUE( collection.restartStopped() );
    SW_EXPECT_STREQ( "Renderer RenderThread SceneDevice", recorder.joinEvents( 'I' ).c_str() );
    SW_EXPECT_TRUE( collection.getStoppedIndices().empty() );

    // 다시 선 것도 성공한 초기화 한 번마다 stop · 종료를 한 번씩 받는다.
    recorder._listEvent.clear();
    collection.shutdownAll();
    SW_EXPECT_STREQ( reverseNames( kRendererShapedOrder ).c_str(), recorder.joinEvents( 'P' ).c_str() );
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
    SW_ASSERT_TRUE( collection.initializeAll( kTestClient ) );
    const ISubsystem* pDevice = nullptr;
    for ( uint32 index = 0; index < collection.getSubsystemCount(); ++index )
    {
        if ( string_view{ collection.getSubsystem( index ).getSubsystemName() } == "Device" )
            pDevice = &collection.getSubsystem( index );
    }
    SW_ASSERT_TRUE( pDevice != nullptr );
    collection.shutdownDependentsOf( *pDevice );
    recorder._failName   = "RenderThread";
    recorder._failResult = SubsystemInitResult::Failed;
    SW_EXPECT_FALSE( collection.restartStopped() );

    recorder._listEvent.clear();
    collection.shutdownAll();
    SW_EXPECT_STREQ( "Tools Scene Resource Renderer Device Config", recorder.joinEvents( 'P' ).c_str() );
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
    SW_ASSERT_TRUE( collection.initializeAll( kTestClient ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 8 ), recorder._listInitializeMemoryTag.size() );
    // 순서: Config Device Renderer RenderThread Resource Scene SceneDevice Tools
    SW_EXPECT_TRUE( recorder._listInitializeMemoryTag[0] == MemoryTag::EngineMisc );
    SW_EXPECT_TRUE( recorder._listInitializeMemoryTag[1] == MemoryTag::RenderCPU );
    SW_EXPECT_TRUE( recorder._listInitializeMemoryTag[4] == MemoryTag::Asset );
    SW_EXPECT_TRUE( recorder._listInitializeMemoryTag[5] == MemoryTag::Scene );
    SW_EXPECT_TRUE( Memory::getCurrentMemoryTag() == MemoryTag::Unknown );
}
