#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/EngineInitSequence.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Text/FontService.h"
#include "Engine/UI/UISystem.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /**
     * @brief 단계가 불린 순서를 이름으로 적습니다 — "I:" 초기화, "S:" 종료, "D:" 해제. 정해 둔 이름의 초기화는 정해 둔 결과를 돌려줍니다.
     * @details 표의 줄은 본문 모음(아래 X-macro)이, 서브시스템은 대역(`StandInSubsystem`)이 같은 목록에 적는다.
     */
    struct StartupRecorder
    {
        EngineInitResult recordInitialize( const utf8* pName )
        {
            _listEvent.push_back( string( "I:" ) + pName );
            _listInitializeName.push_back( pName );
            _listInitializeMemoryTag.push_back( Memory::getCurrentMemoryTag() );
            return _failName == pName ? _failResult : EngineInitResult::Succeeded;
        }
        void recordShutdown( const utf8* pName ) { _listEvent.push_back( string( "S:" ) + pName ); }
        void recordDestroy( const utf8* pName ) { _listEvent.push_back( string( "D:" ) + pName ); }

        /** @brief @p kind 로 시작하는 사건의 이름들을 공백으로 이어 붙입니다. */
        string joinEvents( utf8 kind ) const
        {
            StringBuilder<constant::kMaxBuffer1024> sb;
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

#define SW_ENGINE_STARTUP_STEP( Name, ... )                               \
    static EngineInitResult initialize##Name( StartupRecorder& recorder ) \
    {                                                                     \
        return recorder.recordInitialize( #Name );                        \
    }                                                                     \
    static void shutdown##Name( StartupRecorder& recorder )               \
    {                                                                     \
        recorder.recordShutdown( #Name );                                 \
    }                                                                     \
    static void destroy##Name( StartupRecorder& recorder )                \
    {                                                                     \
        recorder.recordDestroy( #Name );                                  \
    }
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP

        vector<string>      _listEvent{};
        vector<const utf8*> _listInitializeName{};      ///< 초기화한 이름(초기화한 순서)
        vector<MemoryTag>   _listInitializeMemoryTag{}; ///< 초기화 본문이 불린 동안의 할당 태그(`_listInitializeName` 과 같은 순서)
        string              _failName{};
        EngineInitResult    _failResult{ EngineInitResult::Succeeded };
    };

    /** @brief 본문을 RHI 에만 둔 본문 모음입니다 — 나머지 자리는 `EngineInitNoStepBody` 의 null 이다. */
    struct PartialStepBody : EngineInitNoStepBody
    {
        static EngineInitResult initializeRHI( StartupRecorder& recorder ) { return recorder.recordInitialize( "RHI" ); }
        static void             destroyRHI( StartupRecorder& recorder ) { recorder.recordDestroy( "RHI" ); }
    };

    /** @brief 엔진 서브시스템 하나의 선언(이름 · 의존 · 대상 · 태그)을 그대로 지닌 대역입니다. 본문 대신 불린 순서를 적는다. */
    class StandInSubsystem final : public ISubsystem
    {
    public:
        StandInSubsystem( StartupRecorder& recorder, const ISubsystem& real )
            : _recorder{ recorder }
            , _pName{ real.getSubsystemName() }
            , _pDependencies{ real.getSubsystemDependencies() }
            , _target{ real.getSubsystemTarget() }
            , _memoryTag{ real.getSubsystemMemoryTag() }
        {
        }
        ~StandInSubsystem() override { _recorder.recordDestroy( _pName ); }

        EngineInitResult initializeSubsystem() override { return _recorder.recordInitialize( _pName ); }
        void             shutdownSubsystem() override { _recorder.recordShutdown( _pName ); }

        const utf8*      getSubsystemName() const override { return _pName; }
        const utf8*      getSubsystemDependencies() const override { return _pDependencies; }
        EngineInitTarget getSubsystemTarget() const override { return _target; }
        MemoryTag        getSubsystemMemoryTag() const override { return _memoryTag; }

    private:
        StartupRecorder& _recorder;
        const utf8*      _pName;
        const utf8*      _pDependencies;
        EngineInitTarget _target;
        MemoryTag        _memoryTag;
    };

    /**
     * @brief `EngineLoop::initialize` 가 표의 줄과 함께 넣는 서브시스템을 실제 객체로 만듭니다. 엔진 루프에 서브시스템을 더하면 여기도 더한다.
     * @details 클래스가 있는 단계는 그 객체가, 클래스가 없는 단계는 `EngineFunctionStep` 의 선언이 이름 · 의존 · 대상을 말한다.
     */
    vector<unique_ptr<ISubsystem>> makeEngineSubsystems()
    {
        vector<unique_ptr<ISubsystem>> listSubsystem;
        for ( const FunctionSubsystemDesc* pDesc : EngineFunctionStep::kArrAll )
        {
            listSubsystem.push_back( make_unique<FunctionSubsystem>( *pDesc ) );
        }
        listSubsystem.push_back( make_unique<ShaderCache>() );
        listSubsystem.push_back( make_unique<PhysicsSystem>() );
        listSubsystem.push_back( make_unique<FontService>() );
        listSubsystem.push_back( make_unique<TelemetryService>() );
        listSubsystem.push_back( make_unique<UISystem>() );
        return listSubsystem;
    }

    /** @brief 엔진 서브시스템마다 대역을 @p sequence 에 넣습니다 — 표의 줄과 합쳐 엔진의 기동 목록과 같은 그래프다(본문은 돌지 않는다). */
    void addEngineStandIns( EngineInitSequence& sequence, StartupRecorder& recorder )
    {
        for ( const unique_ptr<ISubsystem>& real : makeEngineSubsystems() )
        {
            sequence.addSubsystem( make_unique<StandInSubsystem>( recorder, *real ) );
        }
    }

    /** @brief 순서(자리)를 이름으로 이어 붙입니다. */
    string joinOrderNames( const SubsystemCollection& subsystems )
    {
        StringBuilder<constant::kMaxBuffer1024> sb;
        for ( const uint32 index : subsystems.getInitializeOrder() )
        {
            if ( sb.size() > 0 )
                sb.append( ' ' );
            sb.append( subsystems.getSubsystem( index ).getSubsystemName() );
        }
        return string( sb.c_str() );
    }

    /** @brief 이름 목록(공백으로 구분)을 거꾸로 이어 붙입니다. */
    string reverseNames( const string& names )
    {
        vector<string_view> listName;
        size_t              cursor = 0;
        while ( cursor < names.size() )
        {
            size_t end = names.find( ' ', cursor );
            if ( end == string::npos )
                end = names.size();
            listName.push_back( string_view{ names }.substr( cursor, end - cursor ) );
            cursor = end + 1;
        }
        StringBuilder<constant::kMaxBuffer1024> sb;
        for ( size_t index = listName.size(); index > 0; --index )
        {
            if ( sb.size() > 0 )
                sb.append( ' ' );
            sb.append( listName[index - 1] );
        }
        return string( sb.c_str() );
    }

    /** @brief @p name 의 자리입니다. 없으면 `invalid_index::kUint32` 입니다. */
    uint32 findIndex( const SubsystemCollection& subsystems, string_view name )
    {
        for ( uint32 index = 0; index < subsystems.getSubsystemCount(); ++index )
        {
            if ( string_view{ subsystems.getSubsystem( index ).getSubsystemName() } == name )
                return index;
        }
        return invalid_index::kUint32;
    }

    /** @brief 엔진 목록의 의존 그래프입니다(자리는 `SubsystemCollection` 의 자리). */
    EngineInitGraph makeGraph( const SubsystemCollection& subsystems )
    {
        vector<EngineInitNode> listNode;
        for ( uint32 index = 0; index < subsystems.getSubsystemCount(); ++index )
        {
            listNode.push_back( { subsystems.getSubsystem( index ).getSubsystemName(), subsystems.getSubsystem( index ).getSubsystemDependencies() } );
        }
        EngineInitGraph graph{};
        string          error;
        (void)SubsystemCollection::computeGraph( listNode, graph, error ); // 그래프 오류는 ComputedOrder… 테스트가 본다
        return graph;
    }

    /** @brief @p name 에서 의존을 (간접으로라도) 거슬러 @p dependencyName 에 닿는가 — @p dependencyName 이 먼저 서야 하는가. */
    bool dependsOn( const SubsystemCollection& subsystems, const EngineInitGraph& graph, string_view name, string_view dependencyName )
    {
        const uint32 start      = findIndex( subsystems, name );
        const uint32 dependency = findIndex( subsystems, dependencyName );
        if ( start == invalid_index::kUint32 || dependency == invalid_index::kUint32 )
            return false;
        vector<uint32> listPending{ start };
        vector<uint8>  listVisited( graph._listDependency.size(), SW_FALSE );
        while ( listPending.empty() == false )
        {
            const uint32 nodeIndex = listPending.back();
            listPending.pop_back();
            for ( const uint32 dependencyIndex : graph._listDependency[nodeIndex] )
            {
                if ( dependencyIndex == dependency )
                    return true;
                if ( listVisited[dependencyIndex] == SW_FALSE )
                {
                    listVisited[dependencyIndex] = SW_TRUE;
                    listPending.push_back( dependencyIndex );
                }
            }
        }
        return false;
    }

    /** @brief 초기화 순서의 이름 가운데 @p name 에 (간접으로라도) 의존하는 것을 고릅니다. @p bKeepDependents 가 false 면 거꾸로 그것만 뺍니다. */
    string filterDependentsOf( const SubsystemCollection& subsystems, string_view name, bool bKeepDependents )
    {
        const EngineInitGraph                   graph = makeGraph( subsystems );
        StringBuilder<constant::kMaxBuffer1024> sb;
        for ( const uint32 index : subsystems.getInitializeOrder() )
        {
            const utf8* pName = subsystems.getSubsystem( index ).getSubsystemName();
            if ( dependsOn( subsystems, graph, pName, name ) != bKeepDependents )
                continue;
            if ( sb.size() > 0 )
                sb.append( ' ' );
            sb.append( pName );
        }
        return string( sb.c_str() );
    }

    /** @brief 초기화 순서에서 @p name 까지(포함)의 앞부분입니다. */
    string makeOrderPrefix( const SubsystemCollection& subsystems, string_view name )
    {
        StringBuilder<constant::kMaxBuffer1024> sb;
        for ( const uint32 index : subsystems.getInitializeOrder() )
        {
            const utf8* pName = subsystems.getSubsystem( index ).getSubsystemName();
            if ( sb.size() > 0 )
                sb.append( ' ' );
            sb.append( pName );
            if ( string_view{ pName } == name )
                break;
        }
        return string( sb.c_str() );
    }

    /** @brief 클라이언트에서만 도는 엔진 단계입니다(창 · RHI · 렌더러 · 플레이어 설정 · 글꼴 · 텔레메트리 · UI). */
    constexpr const utf8* kArrClientOnlyName[] = { "UserSettings", "RHI", "FrameRenderer", "RenderThread", "LiveShader", "SceneRHI", "Fonts", "Telemetry", "UI" };
} // namespace

/**
 * @brief [EngineInitSequenceTest] 씬을 읽는 단계(헤드리스 씬 쿠킹)는 모든 타입 공급자가 등록을 끝낸 뒤(`ModuleTypes`)에만 선다
 * @details `Headless` 의 의존이 `ModuleTypes` 를 적고, 그 단계가 실패하면 쿠킹은 돌지 않는다. 쿠킹이 GameFramework · 킷 · 게임 모듈의 타입이
 *          오르기 전에 씬을 읽으면 그 컴포넌트를 `MissingComponent` 로 쿠킹한다. 의존을 빼면 이 테스트가 진다.
 */
SW_TEST_CASE( EngineInitSequenceTest, SceneReadingStepWaitsForModuleTypes )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    const SubsystemCollection& subsystems = sequence.getSubsystems();
    const EngineInitGraph      graph      = makeGraph( subsystems );
    SW_EXPECT_TRUE( dependsOn( subsystems, graph, "Headless", "ModuleTypes" ) );
    // 타입 등록은 리플렉션 · 설정(키트 목록) 뒤다 — 모듈 이미지를 내리는 자리(ModuleImages)보다도 뒤라 종료에서 먼저 내려간다.
    SW_EXPECT_TRUE( dependsOn( subsystems, graph, "ModuleTypes", "Reflection" ) );
    SW_EXPECT_TRUE( dependsOn( subsystems, graph, "ModuleTypes", "ModuleImages" ) );

    recorder._failName   = "ModuleTypes";
    recorder._failResult = EngineInitResult::Failed;
    SW_EXPECT_FALSE( sequence.initializeAll<StartupRecorder>( recorder ) );
    for ( const utf8* pName : recorder._listInitializeName )
    {
        SW_EXPECT_TRUE_MSG( string_view{ pName } != "Headless", "the scene cook ran although module types were not registered" );
    }
    sequence.destroyAll();
}

/**
 * @brief [EngineInitSequenceTest] 엔진 기동 순서(표의 줄 + 서브시스템)는 오류 없이 계산되고, 결정적이며, 모든 의존이 앞에 선다
 * @details 순서의 진실은 의존 하나다 — 추가 순서는 보지 않는다. 같은 목록을 다시 만들어도 같은 순서가 나와야 하고(동점은 이름 순),
 *          어느 단계든 그 의존은 순서에서 그보다 앞이다. 의존 이름이 틀리거나 대상이 어긋나면 여기서 진다.
 */
SW_TEST_CASE( EngineInitSequenceTest, ComputedOrderIsDeterministicAndDependenciesComeFirst )
{
    StartupRecorder    recorder;
    EngineInitSequence sequence;
    addEngineStandIns( sequence, recorder );
    SW_ASSERT_TRUE_MSG( sequence.getError().empty(), sequence.getError().c_str() );
    const SubsystemCollection& subsystems = sequence.getSubsystems();
    SW_ASSERT_EQUAL( subsystems.getSubsystemCount(), static_cast<uint32>( subsystems.getInitializeOrder().size() ) );

    EngineInitSequence other;
    addEngineStandIns( other, recorder );
    SW_EXPECT_STREQ( joinOrderNames( subsystems ).c_str(), joinOrderNames( other.getSubsystems() ).c_str() );

    const EngineInitGraph graph = makeGraph( subsystems );
    vector<uint32>        listPosition( subsystems.getSubsystemCount(), 0 );
    for ( uint32 position = 0; position < static_cast<uint32>( subsystems.getInitializeOrder().size() ); ++position )
    {
        listPosition[subsystems.getInitializeOrder()[position]] = position;
    }
    for ( uint32 index = 0; index < subsystems.getSubsystemCount(); ++index )
    {
        for ( const uint32 dependencyIndex : graph._listDependency[index] )
        {
            SW_EXPECT_TRUE_MSG( listPosition[dependencyIndex] < listPosition[index], subsystems.getSubsystem( index ).getSubsystemName() );
        }
    }
}

/**
 * @brief [EngineInitSequenceTest] 종료가 초기화의 정확한 역순이고, 해제는 모든 종료 뒤에 모든 단계를 역순으로 돈다(표의 줄과 서브시스템이 섞여도)
 */
SW_TEST_CASE( EngineInitSequenceTest, ShutdownAndDestroyRunInReverseOfInitialization )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    SW_ASSERT_TRUE( sequence.initializeAll<StartupRecorder>( recorder ) );
    const string order = joinOrderNames( sequence.getSubsystems() );
    SW_EXPECT_STREQ( order.c_str(), recorder.joinEvents( 'I' ).c_str() );

    sequence.shutdownAll();
    sequence.shutdownAll(); // 두 번 불러도 다시 내리지 않는다
    SW_EXPECT_STREQ( reverseNames( order ).c_str(), recorder.joinEvents( 'S' ).c_str() );

    sequence.destroyAll();
    SW_EXPECT_STREQ( reverseNames( order ).c_str(), recorder.joinEvents( 'D' ).c_str() );
    // 종료가 모두 해제보다 앞이다.
    bool bDestroySeen = false;
    for ( const string& event : recorder._listEvent )
    {
        bDestroySeen = bDestroySeen || event[0] == 'D';
        SW_EXPECT_TRUE_MSG( bDestroySeen == false || event[0] == 'D', event.c_str() );
    }
}

/**
 * @brief [EngineInitSequenceTest] 단계가 실패하면 거기서 멈추고, 초기화한 단계만 역순으로 내리며, 해제는 모든 단계에 돈다
 */
SW_TEST_CASE( EngineInitSequenceTest, FailedStepStopsAndShutsDownOnlyInitializedSteps )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    recorder._failName   = "Scene";
    recorder._failResult = EngineInitResult::Failed;
    SW_EXPECT_FALSE( sequence.initializeAll<StartupRecorder>( recorder ) );
    const string prefix = makeOrderPrefix( sequence.getSubsystems(), "Scene" );
    SW_EXPECT_STREQ( prefix.c_str(), recorder.joinEvents( 'I' ).c_str() );

    const string order = joinOrderNames( sequence.getSubsystems() );
    sequence.shutdownAll();
    const string withoutScene = string( string_view{ prefix }.substr( 0, prefix.size() - string_view{ " Scene" }.size() ) ); // 실패한 Scene 은 종료를 받지 않는다
    SW_EXPECT_STREQ( reverseNames( withoutScene ).c_str(), recorder.joinEvents( 'S' ).c_str() );

    // 해제는 실패한 단계와 닿지 못한 단계까지 모두 돈다. 부트스트랩이 미리 만든 객체가 있기 때문이다.
    sequence.destroyAll();
    SW_EXPECT_STREQ( reverseNames( order ).c_str(), recorder.joinEvents( 'D' ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] `SkipDependents`(헤드리스 작업)가 그 단계에 의존하는 단계만 건너뛰는지 검증
 * @details 셰이더 쿠킹 · 씬 쿠킹은 RHI · 렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬 디바이스를 세우지 않는다. 건너뛴 단계는 종료도 하지 않는다.
 */
SW_TEST_CASE( EngineInitSequenceTest, SkipDependentsSkipsEveryDependentStep )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    recorder._failName   = "Headless";
    recorder._failResult = EngineInitResult::SkipDependents;
    SW_EXPECT_TRUE( sequence.initializeAll<StartupRecorder>( recorder ) );
    const SubsystemCollection& subsystems = sequence.getSubsystems();
    const string               expected   = filterDependentsOf( subsystems, "Headless", false );
    SW_EXPECT_STREQ( expected.c_str(), recorder.joinEvents( 'I' ).c_str() );
    const EngineInitGraph graph = makeGraph( subsystems );
    for ( const utf8* pName : { "RHI", "FrameRenderer", "RenderThread", "LiveShader", "SceneRHI" } )
    {
        SW_EXPECT_TRUE_MSG( dependsOn( subsystems, graph, pName, "Headless" ), pName );
    }

    sequence.shutdownAll();
    SW_EXPECT_STREQ( reverseNames( expected ).c_str(), recorder.joinEvents( 'S' ).c_str() );
    sequence.destroyAll();
}

/**
 * @brief [EngineInitSequenceTest] 본문 모음이 정의하지 않은 표의 자리는 부르지 않고, 순서 · 판정은 그대로인지 검증
 * @details 표의 줄을 더할 때 호스트마다 타입을 짓지 않는다 — 본문 모음(`EngineInitNoStepBody` 상속)이 필요한 함수만 둔다.
 */
SW_TEST_CASE( EngineInitSequenceTest, MissingBodiesAreSkipped )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    SW_ASSERT_TRUE( sequence.initializeAll<PartialStepBody>( recorder ) );
    // 본문이 없는 표의 줄도 초기화된 것으로 친다 — 종료 · 재시작 판정이 본문 유무에 따라 달라지지 않는다.
    SW_EXPECT_EQUAL( static_cast<size_t>( EngineInitStep::Count ), sequence.getInitializedSteps().size() );
    sequence.destroyAll();

    // 표의 줄 가운데 본문을 둔 RHI 만 적힌다(서브시스템 대역은 제 이름으로 적는다).
    for ( uint32 stepIndex = 0; stepIndex < static_cast<uint32>( EngineInitStep::Count ); ++stepIndex )
    {
        const utf8* const pName           = EngineInitSequence::getStepName( static_cast<EngineInitStep>( stepIndex ) );
        const uint32      expectedCount   = string_view{ pName } == "RHI" ? 1u : 0u;
        uint32            initializeCount = 0;
        uint32            shutdownCount   = 0;
        uint32            destroyCount    = 0;
        for ( const string& event : recorder._listEvent )
        {
            if ( string_view{ event }.substr( 2 ) != pName )
                continue;
            initializeCount += event[0] == 'I' ? 1u : 0u;
            shutdownCount += event[0] == 'S' ? 1u : 0u;
            destroyCount += event[0] == 'D' ? 1u : 0u;
        }
        SW_EXPECT_TRUE_MSG( initializeCount == expectedCount, pName );
        SW_EXPECT_TRUE_MSG( shutdownCount == 0u, pName );
        SW_EXPECT_TRUE_MSG( destroyCount == expectedCount, pName );
    }
}

/**
 * @brief [EngineInitSequenceTest] 호스트를 받기 전(`initializeAll` 전)의 해제는 표의 본문을 부르지 않는지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, DestroyBeforeInitializeDoesNothing )
{
    EngineInitSequence sequence;
    sequence.shutdownAll();
    sequence.destroyAll();
    SW_EXPECT_TRUE( sequence.getInitializedSteps().empty() );
}

/**
 * @brief [EngineInitSequenceTest] 순환은 정렬 오류이고, 순환에 든 노드를 이름으로 알리는지 검증
 * @details 표(`EngineInitStepList.xxx`)의 줄과 서브시스템이 같은 판정을 받는다.
 */
SW_TEST_CASE( EngineInitSequenceTest, CycleIsRejected )
{
    const vector<EngineInitNode> listNode = {
        {"A", "C"},
        {"B", "A"},
        {"C", "B"},
        {"D",  ""}
    };
    EngineInitGraph graph{};
    string          error;
    SW_EXPECT_FALSE( SubsystemCollection::computeGraph( listNode, graph, error ) );
    SW_EXPECT_STREQ( "Startup steps in or behind a dependency cycle: A B C", error.c_str() );
    SW_EXPECT_EMPTY( graph._listOrder );
}

/**
 * @brief [EngineInitSequenceTest] 모르는 의존 이름 · 같은 이름 두 번은 정렬 오류인지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, UnknownOrDuplicateNameIsRejected )
{
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

    // 표의 줄도 같은 판정이다 — 서브시스템을 넣지 않은 표만으로는 표가 부르는 서브시스템 이름을 모른다.
    const EngineInitSequence tableOnly;
    SW_EXPECT_FALSE( tableOnly.getError().empty() );
}

/**
 * @brief [EngineInitSequenceTest] 의존이 순서를 정하고, 의존이 없는 노드끼리는 이름 순으로 가는지 검증(목록 순서는 보지 않는다)
 * @details 목록은 이름과 거꾸로 두고, 의존 글은 표의 꼴(`{ A, B }`)과 공백 꼴을 섞는다.
 */
SW_TEST_CASE( EngineInitSequenceTest, DependenciesOrderAndNameBreaksTies )
{
    const vector<EngineInitNode> listNode = {
        {"D", "{ A, B }"},
        {"C",         ""},
        {"B",       "{}"},
        {"A",      " C "}
    };
    EngineInitGraph graph{};
    string          error;
    SW_ASSERT_TRUE_MSG( SubsystemCollection::computeGraph( listNode, graph, error ), error.c_str() );
    string order;
    for ( const uint32 nodeIndex : graph._listOrder )
    {
        if ( order.empty() == false )
            order += " ";
        order += listNode[nodeIndex]._pName;
    }
    SW_EXPECT_STREQ( "B C A D", order.c_str() );
}

/**
 * @brief [EngineInitSequenceTest] RHI 에 (간접으로라도) 의존하는 단계만 역순으로 내리고, 같은 본문으로 다시 세우는지 검증
 * @details 백엔드 교체가 이 길을 탄다: RHI 의 디바이스를 갈아 끼우는 동안 렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬의 디바이스만 내렸다가
 *          다시 세운다. 다시 세운 뒤의 종료 순서는 처음 기동한 것과 같아야 한다.
 */
SW_TEST_CASE( EngineInitSequenceTest, DependentsOfAStepRestartWithTheSameBodies )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    SW_ASSERT_TRUE( sequence.initializeAll<StartupRecorder>( recorder ) );
    const string order     = joinOrderNames( sequence.getSubsystems() );
    const string dependent = filterDependentsOf( sequence.getSubsystems(), "RHI", true );
    SW_EXPECT_STREQ( "FrameRenderer RenderThread LiveShader SceneRHI", dependent.c_str() );
    recorder._listEvent.clear();

    sequence.shutdownDependentsOf( EngineInitStep::RHI );
    SW_EXPECT_STREQ( reverseNames( dependent ).c_str(), recorder.joinEvents( 'S' ).c_str() );
    SW_EXPECT_TRUE( recorder.joinEvents( 'D' ).empty() );

    SW_EXPECT_TRUE( sequence.restartStoppedSteps() );
    SW_EXPECT_STREQ( dependent.c_str(), recorder.joinEvents( 'I' ).c_str() );
    SW_EXPECT_TRUE( sequence.getStoppedSteps().empty() );

    recorder._listEvent.clear();
    sequence.shutdownAll();
    SW_EXPECT_STREQ( reverseNames( order ).c_str(), recorder.joinEvents( 'S' ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 다시 세우다 실패하면 거기서 멈추고, 서지 못한 단계는 종료 대상에서 빠지는지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, FailedRestartLeavesTheRestStopped )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    SW_ASSERT_TRUE( sequence.initializeAll<StartupRecorder>( recorder ) );

    sequence.shutdownDependentsOf( EngineInitStep::RHI );
    recorder._failName   = "RenderThread";
    recorder._failResult = EngineInitResult::Failed;
    SW_EXPECT_FALSE( sequence.restartStoppedSteps() );

    // 실패한 RenderThread 와 그 뒤로 다시 세우지 못한 단계(라이브 셰이더 · 씬의 디바이스)는 종료 대상이 아니다.
    recorder._listEvent.clear();
    sequence.shutdownAll();
    const string shutdown = recorder.joinEvents( 'S' );
    SW_EXPECT_TRUE( shutdown.find( "FrameRenderer" ) != string::npos );
    for ( const utf8* pName : { "RenderThread", "LiveShader", "SceneRHI" } )
    {
        SW_EXPECT_TRUE_MSG( shutdown.find( pName ) == string::npos, pName );
    }
}

/**
 * @brief [EngineInitSequenceTest] 단계 초기화(기동 · 재시작)는 그 단계의 메모리 태그 아래에서 돈다(표의 줄 · 서브시스템 모두)
 * @details 단계 초기화가 잡는 메모리(태스크 큐 · 타입 표 · 디바이스 · 렌더러)가 용도 줄로 세이는 근거다. 백엔드 교체처럼 단계를 다시 세울 때도 같다.
 */
SW_TEST_CASE( EngineInitSequenceTest, InitializeRunsUnderTheStepMemoryTag )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );

    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    SW_ASSERT_TRUE( sequence.initializeAll<StartupRecorder>( recorder ) );
    sequence.shutdownDependentsOf( EngineInitStep::RHI );
    SW_ASSERT_TRUE( sequence.restartStoppedSteps() );
    const SubsystemCollection& subsystems = sequence.getSubsystems();
    SW_ASSERT_TRUE( recorder._listInitializeName.size() > subsystems.getSubsystemCount() );
    SW_ASSERT_EQUAL( recorder._listInitializeName.size(), recorder._listInitializeMemoryTag.size() );

    uint32 wrongCount{ 0 };
    for ( size_t order = 0; order < recorder._listInitializeName.size(); ++order )
    {
        const uint32 index = findIndex( subsystems, recorder._listInitializeName[order] );
        SW_ASSERT_TRUE( index != invalid_index::kUint32 );
        if ( recorder._listInitializeMemoryTag[order] != subsystems.getSubsystem( index ).getSubsystemMemoryTag() )
            ++wrongCount;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );
    SW_EXPECT_TRUE( Memory::getCurrentMemoryTag() == MemoryTag::Unknown );
    sequence.destroyAll();
}

/**
 * @brief [EngineInitSequenceTest] 전용 서버(대상 Server)는 창 · RHI · 렌더러 · 플레이어 설정 · 글꼴 · 텔레메트리 · UI 를 돌리지 않고, 씬 · 씬 쿠킹까지는 선다
 * @details 서버에 GPU 가 없으므로 RHI 단계가 돌면 기동이 실패한다. 해제는 기동이 어디까지 갔든 모든 단계를 돈다.
 */
SW_TEST_CASE( EngineInitSequenceTest, DedicatedServerSkipsClientSteps )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    SW_ASSERT_TRUE( sequence.initializeAll<StartupRecorder>( recorder, EngineInitTarget::Server ) );

    bool bSceneInitialized    = false;
    bool bHeadlessInitialized = false;
    for ( const utf8* pName : recorder._listInitializeName )
    {
        bSceneInitialized    = bSceneInitialized || string_view{ pName } == "Scene";
        bHeadlessInitialized = bHeadlessInitialized || string_view{ pName } == "Headless";
        for ( const utf8* pClientName : kArrClientOnlyName )
        {
            SW_EXPECT_TRUE_MSG( string_view{ pName } != pClientName, pName );
        }
    }
    SW_EXPECT_TRUE( bSceneInitialized );
    SW_EXPECT_TRUE( bHeadlessInitialized );
    const SubsystemCollection& subsystems = sequence.getSubsystems();
    SW_EXPECT_EQUAL( static_cast<size_t>( subsystems.getSubsystemCount() ) - SW_COUNT_OF( kArrClientOnlyName ), recorder._listInitializeName.size() );
    for ( const utf8* pClientName : kArrClientOnlyName )
    {
        const uint32 index = findIndex( subsystems, pClientName );
        SW_ASSERT_TRUE_MSG( index != invalid_index::kUint32, pClientName );
        SW_EXPECT_TRUE_MSG( subsystems.getSubsystem( index ).getSubsystemTarget() == EngineInitTarget::Client, pClientName );
    }

    const string order = joinOrderNames( subsystems );
    sequence.shutdownAll();
    SW_EXPECT_STREQ( reverseNames( recorder.joinEvents( 'I' ) ).c_str(), recorder.joinEvents( 'S' ).c_str() );
    sequence.destroyAll();
    SW_EXPECT_STREQ( reverseNames( order ).c_str(), recorder.joinEvents( 'D' ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 클라이언트(App · 테스트 하네스)는 모든 단계를 돌린다 — 대상이 기본 기동을 바꾸지 않는다
 */
SW_TEST_CASE( EngineInitSequenceTest, ClientRunsEveryStep )
{
    EngineInitSequence sequence;
    StartupRecorder    recorder;
    addEngineStandIns( sequence, recorder );
    SW_ASSERT_TRUE( sequence.initializeAll<StartupRecorder>( recorder ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( sequence.getSubsystems().getSubsystemCount() ), recorder._listInitializeName.size() );
    sequence.destroyAll();
}

/**
 * @brief [EngineInitSequenceTest] 엔진이 넘기는 실제 서브시스템 객체가 표의 줄과 오류 없이 한 순서로 정렬되고, 클래스 단계의 선언이 대역과 같다
 * @details 의존 글은 실행 시간에만 풀린다. 의존 이름이 틀리거나 대상이 어긋나면 여기서 진다. 의존이 있는 단계는 그 의존이 모두 앞에 선다.
 */
SW_TEST_CASE( EngineInitSequenceTest, EngineSubsystemsJoinTheTableOrder )
{
    EngineInitSequence sequence;
    for ( unique_ptr<ISubsystem>& real : makeEngineSubsystems() )
    {
        sequence.addSubsystem( std::move( real ) );
    }
    SW_ASSERT_TRUE_MSG( sequence.getError().empty(), sequence.getError().c_str() );

    const SubsystemCollection& subsystems = sequence.getSubsystems();
    const EngineInitGraph      graph      = makeGraph( subsystems );
    vector<uint32>             listPosition( subsystems.getSubsystemCount(), 0 );
    for ( uint32 position = 0; position < static_cast<uint32>( subsystems.getInitializeOrder().size() ); ++position )
    {
        listPosition[subsystems.getInitializeOrder()[position]] = position;
    }
    for ( uint32 index = 0; index < subsystems.getSubsystemCount(); ++index )
    {
        for ( const uint32 dependencyIndex : graph._listDependency[index] )
        {
            SW_EXPECT_TRUE_MSG( listPosition[dependencyIndex] < listPosition[index], subsystems.getSubsystem( index ).getSubsystemName() );
        }
    }
    // 서브시스템이 가는 자리는 대역 목록과 같다 — 대역으로 본 순서 테스트가 실제 객체에도 맞는다.
    StartupRecorder    recorder;
    EngineInitSequence standIns;
    addEngineStandIns( standIns, recorder );
    SW_EXPECT_STREQ( joinOrderNames( standIns.getSubsystems() ).c_str(), joinOrderNames( subsystems ).c_str() );
}
