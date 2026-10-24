#include "pch.h"

#include "Engine/Renderer/Graph/RenderGraph.h"

#include "Core/Container/VectorUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Profiling/FrameProfiler.h"

#include <queue>

namespace sw
{
    namespace
    {
        void recordRenderPass( RenderGraphNode* pNode, IRHICommandList* pCmdList, bool bAlreadyBegun )
        {
            const bool bNodeNotRecordable = pNode == nullptr || pCmdList == nullptr || pNode->_execute.isBound() == false;
            if ( bNodeNotRecordable )
                return;

            // 레벨의 첫 리스트는 렌더 스레드가 이미 열어 배리어를 앞머리에 기록해 뒀다. 이어서 기록한다.
            if ( bAlreadyBegun == false )
                pCmdList->beginCommandList();
            RenderGraphPassContext passContext;
            passContext._passName     = pNode->_name;
            passContext._pListInputs  = &pNode->_listInput;
            passContext._pListOutputs = &pNode->_listOutput;
            passContext._pCmdList     = pCmdList;
            pNode->_execute( passContext );
            pCmdList->endCommandList();
        }

        /**
         * @brief `RenderGraph::compile` 의 단계들입니다. 활성 노드 사이의 의존 간선을 모으고, Kahn 위상 정렬을 레벨 단위로 돌리고, 순환이면 이름으로 알립니다.
         */
        struct RenderGraphCompileInternal
        {
            /** @brief 생산자에서 소비자로 향하는 인접 목록과 활성 노드 기준 진입 차수입니다. */
            struct DependencyGraph
            {
                unordered_map<size_t, vector<size_t>> _adjacency;
                unordered_map<size_t, uint32>         _mapInDegree;

                void addEdge( size_t from, size_t to )
                {
                    if ( from == to )
                        return;
                    vector<size_t>& listAdjacent = _adjacency[from];
                    if ( VectorUtil::contains( listAdjacent, to ) == false )
                    {
                        listAdjacent.push_back( to );
                        ++_mapInDegree[to];
                    }
                }
            };

            /** @brief 간선을 모읍니다 — 1) 같은 리소스의 쓰기 사슬(WAW), 2) 읽기와 그 생산자(RAW), 3) 그 읽기와 생산자 다음 쓰기(WAR). */
            static void collectDependencyEdges( const vector<RenderGraphNode>& listNode, const vector<size_t>& listActiveIndex, DependencyGraph& outGraph )
            {
                // 1. 리소스마다 그것을 쓰는 패스를 활성 등록 순서대로 모두 모은다.
                unordered_map<hashed_string, vector<size_t>> mapResourceWriter;
                for ( size_t nodeIndex : listActiveIndex )
                {
                    for ( const hashed_string& output : listNode[nodeIndex]._listOutput )
                    {
                        mapResourceWriter[output].push_back( nodeIndex );
                    }
                }

                outGraph._adjacency.reserve( listActiveIndex.size() );
                outGraph._mapInDegree.reserve( listActiveIndex.size() );
                for ( size_t nodeIndex : listActiveIndex )
                {
                    outGraph._mapInDegree[nodeIndex] = 0;
                }

                // 2. 같은 리소스를 잇따라 쓰는 패스들을 사슬로 연결한다(Write-after-Write 순서).
                for ( const auto& [resource, listWriter] : mapResourceWriter )
                {
                    for ( size_t writerIndex = 0; writerIndex + 1 < listWriter.size(); ++writerIndex )
                    {
                        outGraph.addEdge( listWriter[writerIndex], listWriter[writerIndex + 1] );
                    }
                }

                // 3. 입력을 읽는 소비자마다 맞는 생산자를 찾는다.
                for ( size_t consumerIndex : listActiveIndex )
                {
                    for ( const hashed_string& input : listNode[consumerIndex]._listInput )
                    {
                        auto it = mapResourceWriter.find( input );
                        if ( it == mapResourceWriter.end() || it->second.empty() )
                            continue;

                        const auto& listWriter    = it->second;
                        size_t      producerIndex = listWriter.front();
                        for ( size_t writerIndex : listWriter )
                        {
                            if ( writerIndex < consumerIndex )
                                producerIndex = writerIndex;
                        }
                        outGraph.addEdge( producerIndex, consumerIndex );

                        // 이 읽기가 본 버전(생산자) **다음에** 같은 자원을 덮어쓰는 패스는 이 읽기 뒤에 와야 한다(Write-after-Read). 이
                        // 간선이 없으면 W 가 쓰고 A 가 읽고 B 가 다시 쓸 때 A 와 B 가 같은 레벨에 들어간다. 직렬로는 B 가 먼저 돌아 A 가 B 의 출력을
                        // 읽고, 병렬로는 둘이 경합한다. 기준은 선언 순서가 아니라 **쓰기 사슬에서 생산자
                        // 다음**이다. 소비자를 생산자보다 먼저 선언해도 되는 그래프라(앞에 쓰는 이가 없으면 첫 쓰기가 생산자), 선언 순서로 고르면
                        // 생산자 자신을 골라 순환이 된다.
                        for ( size_t chainIndex = 0; chainIndex + 1 < listWriter.size(); ++chainIndex )
                        {
                            if ( listWriter[chainIndex] == producerIndex )
                            {
                                outGraph.addEdge( consumerIndex, listWriter[chainIndex + 1] );
                                break;
                            }
                        }
                    }
                }
            }

            /**
             * @brief Kahn 위상 정렬을 BFS 레벨 단위로 묶어 처리합니다.
             * @details 같은 레벨에 들어온 노드들은 서로 입출력 의존이 없어(동시에 in-degree 가 0이 됨) 안전하게 병렬 기록할 수 있다.
             */
            static void scheduleLevels( const vector<RenderGraphNode>& listNode, const vector<size_t>& listActiveIndex, DependencyGraph& graph,
                                        vector<hashed_string>& outListOrder, vector<vector<hashed_string>>& outListLevel )
            {
                std::queue<size_t> queueReady;
                for ( size_t nodeIndex : listActiveIndex )
                {
                    if ( graph._mapInDegree[nodeIndex] == 0 )
                        queueReady.push( nodeIndex );
                }

                outListOrder.reserve( listActiveIndex.size() );
                while ( queueReady.empty() == false )
                {
                    const size_t           levelSize = queueReady.size();
                    vector<hashed_string>& level     = outListLevel.emplace_back();
                    level.reserve( levelSize );

                    for ( size_t levelSlot = 0; levelSlot < levelSize; ++levelSlot )
                    {
                        const size_t nodeIndex = queueReady.front();
                        queueReady.pop();
                        outListOrder.push_back( listNode[nodeIndex]._name );
                        level.push_back( listNode[nodeIndex]._name );

                        const auto adjacentIt = graph._adjacency.find( nodeIndex );
                        if ( adjacentIt == graph._adjacency.end() )
                            continue;

                        for ( size_t consumerIndex : adjacentIt->second )
                        {
                            uint32& degree = graph._mapInDegree[consumerIndex];
                            if ( degree > 0 )
                                --degree;
                            if ( degree == 0 )
                                queueReady.push( consumerIndex );
                        }
                    }
                }
            }

            /**
             * @brief 순환을 이름으로 알립니다. 남은 패스는 진입 차수가 0이 되지 못한 것이고, 그 사이의 간선이 순환(과 그 뒤에 매달린 패스)이다.
             * @details "몇 개 중 몇 개" 만으로는 파이프라인 XML 의 입출력을 손으로 따라가야 하므로 무엇이 무엇을 기다리다 남았는지 적는다.
             */
            static void reportCycle( const vector<RenderGraphNode>& listNode, DependencyGraph& graph, size_t scheduledCount, size_t activeCount )
            {
                StringBuilder<constant::kMaxBuffer4096> waits;
                for ( const auto& [producerIndex, listConsumer] : graph._adjacency )
                {
                    if ( graph._mapInDegree[producerIndex] == 0 )
                        continue;
                    for ( size_t consumerIndex : listConsumer )
                    {
                        if ( graph._mapInDegree[consumerIndex] > 0 )
                            waits.appendFormat( "\n  '%#' waits on '%#'", listNode[consumerIndex]._name.c_str(), listNode[producerIndex]._name.c_str() );
                    }
                }
                SW_LOG_ERROR_ID( "Renderer.GraphCycle", "Render graph has a cycle - %#/%# active passes scheduled; these wait on each other "
                                                        "(a pass reads what a later pass writes, which reads this one's output):%#",
                                 static_cast<uint32>( scheduledCount ), static_cast<uint32>( activeCount ), waits.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "RenderGraph" );

    /**
     * @brief 병렬 기록 한 레벨의 패스와 그것을 기록할 리스트입니다. 리스트는 노드가 가지고 있는 것을 빌립니다(소유하지 않습니다).
     * @details 태스크는 이 엔트리의 메서드에 묶인다(`record`). 메서드 델리게이트는 포인터 둘이라 힙도 노드 팽창도 없다.
     *          주의: `MakeTaskArgs` 로 인자를 실으면 패스마다 프레임마다 힙 할당이 생기고, 인자를 노드 안에 인라인으로 넣으면 노드가 두 배로
     *          부풀어 렌더 그래프 기록이 122µs 에서 196µs 로 느려진다.
     *          엔트리 목록은 태스크를 넣기 전에 모두 채우므로 기록 중에 옮겨지지 않는다.
     */
    struct ParallelPassEntry
    {
        RenderGraphNode* _pNode{ nullptr };
        IRHICommandList* _pPassCmdList{ nullptr };
        /// @brief 렌더 스레드가 이미 열고 레벨 배리어를 기록한 리스트인지 나타냅니다(레벨의 첫 엔트리입니다).
        bool _bAlreadyBegun{ false };

        void record() { recordRenderPass( _pNode, _pPassCmdList, _bAlreadyBegun ); }
    };
} // namespace sw

namespace sw
{
    struct RenderGraph::ParallelScratch
    {
        vector<ParallelPassEntry> _listPassEntry;
        /// @brief 노드 인덱스에서 그 패스가 프레임마다 다시 여는 커맨드 리스트를 찾는 목록입니다. 처음 쓸 때 만들고 그 뒤로 다시 씁니다.
        vector<unique_ptr<IRHICommandList>> _listNodeCmdList;
        /// @brief 리스트를 만든 디바이스입니다. 다른 디바이스가 오면 먼저 해제합니다(백엔드 교체).
        IRHIDevice* _pCmdListDevice{ nullptr };
    };

    RenderGraph::RenderGraph()
        : _pParallelScratch{ make_unique<ParallelScratch>() }
    {
    }

    RenderGraph::~RenderGraph()                                   = default;
    RenderGraph::RenderGraph( RenderGraph&& ) noexcept            = default;
    RenderGraph& RenderGraph::operator=( RenderGraph&& ) noexcept = default;

    void RenderGraph::releaseCommandLists()
    {
        if ( _pParallelScratch == nullptr )
            return;
        _pParallelScratch->_listNodeCmdList.clear();
        _pParallelScratch->_listPassEntry.clear();
        _pParallelScratch->_pCmdListDevice = nullptr;
    }

    /**
     * @brief 그래프에 새 패스 노드를 등록합니다.
     */
    bool RenderGraph::addPass( hashed_string passName, vector<hashed_string> listInput, vector<hashed_string> listOutput,
                               RenderGraphPassExecuteFn execute )
    {
        // 이름은 패스의 키다(실행 순서, 레벨, 이름에서 노드를 찾는 테이블). 같은 이름을 받으면 그 테이블이 뒤의 것으로 덮여 한 패스는 두 번,
        // 다른 하나는 한 번도 돌지 않고, 병렬 기록에서는 두 작업 스레드가 같은 커맨드 리스트에 쓴다. 그래서 거절한다.
        for ( const RenderGraphNode& existing : _listNode )
        {
            if ( existing._name == passName )
            {
                SW_LOG_ERROR( "Render graph pass '%#' is declared twice - the second declaration is ignored", passName.c_str() );
                return false;
            }
        }

        RenderGraphNode node;
        node._name       = passName;
        node._listInput  = std::move( listInput );
        node._listOutput = std::move( listOutput );
        node._execute    = std::move( execute );
        node._bCulled    = false;
        _listNode.push_back( std::move( node ) );
        return true;
    }

    /**
     * @brief 그래프를 컴파일합니다. 리소스 의존성 기준 Kahn 위상 정렬로 실행 순서를 만듭니다.
     */
    bool RenderGraph::compile()
    {
        using Internal = RenderGraphCompileInternal;
        _listCompiledExecutionOrder.clear();
        _listCompiledLevel.clear();

        if ( _listNode.empty() )
            return false;

        // 활성(컬링되지 않은) 노드 인덱스
        vector<size_t> listActiveIndex;
        listActiveIndex.reserve( _listNode.size() );
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( _listNode[nodeIndex]._bCulled == false )
                listActiveIndex.push_back( nodeIndex );
        }
        if ( listActiveIndex.empty() )
            return false;

        Internal::DependencyGraph graph;
        Internal::collectDependencyEdges( _listNode, listActiveIndex, graph );
        Internal::scheduleLevels( _listNode, listActiveIndex, graph, _listCompiledExecutionOrder, _listCompiledLevel );
        if ( _listCompiledExecutionOrder.size() != listActiveIndex.size() )
        {
            Internal::reportCycle( _listNode, graph, _listCompiledExecutionOrder.size(), listActiveIndex.size() );
            _listCompiledExecutionOrder.clear();
            _listCompiledLevel.clear();
            return false;
        }

        _mapNameToIndex.clear();
        _mapNameToIndex.reserve( _listNode.size() );
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            _mapNameToIndex[_listNode[nodeIndex]._name] = nodeIndex;
        }

        // 수명은 실행 순서가 정해진 **뒤에야** 의미가 있다. "몇 번째 패스에서 처음 또는 마지막으로 쓰이나" 이기 때문이다.
        computeResourceLifetimes();

        return true;
    }

    /**
     * @brief 컴파일된 위상 순서로 패스 콜백을 실행하고 논리 리소스 전이를 추적합니다.
     */
    bool RenderGraph::execute( RenderGraphExecutionContext& context, IRHICommandList* pCmdList )
    {
        context.reset();

        if ( _listCompiledExecutionOrder.empty() )
        {
            if ( compile() == false )
                return false;
        }

        for ( const hashed_string& passName : _listCompiledExecutionOrder )
        {
            unordered_map<hashed_string, size_t>::iterator indexIt = _mapNameToIndex.find( passName );
            if ( indexIt == _mapNameToIndex.end() )
            {
                SW_LOG_ERROR( "execute: unknown pass in order: %#", passName.c_str() );
                return false;
            }

            RenderGraphNode& node = _listNode[indexIt->second];
            if ( node._bCulled )
                continue;

            // 직렬 경로도 **같은 추론**을 쓴다. 상태만 기록하고 배리어를 내지 않으면 전이가 패스 콜백 안 여기저기에서
            // 즉흥적으로 일어난다. 경로가 둘이면 한쪽에만 수정이 들어간다.
            // 배리어는 이 패스가 기록하는 것과 **같은 리스트**에 들어가야 한다(레벨처럼 앞으로 몰 수 없다).
            _listLevelBarrier.clear();
            _mapLevelBarrierIndex.clear();
            appendPassBarriers( context, node );
            issueBarriers( pCmdList );

            if ( node._execute.isBound() )
            {
                RenderGraphPassContext passContext;
                passContext._passName     = node._name;
                passContext._pListInputs  = &node._listInput;
                passContext._pListOutputs = &node._listOutput;
                node._execute( passContext );
            }
        }

        return true;
    }

    bool RenderGraph::executeParallel( RenderGraphExecutionContext& context, TaskManager* pTaskManager, IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.Graph.executeParallel" );

        if ( _listCompiledExecutionOrder.empty() && compile() == false )
            return false;

        const bool bSequentialExecution = pTaskManager == nullptr || pDevice == nullptr || _listCompiledExecutionOrder.size() <= 1 ||
                                          pDevice->getCapabilities()._bParallelCommandRecording == SW_FALSE;
        if ( bSequentialExecution )
            return execute( context );

        context.reset();
        if ( prepareParallelCommandLists( pDevice ) == false )
            return false;

        // 의존성 레벨 단위로 처리한다. 같은 레벨의 패스들만 동시에 병렬 기록하고,
        // 레벨 경계마다 태스크를 기다린 뒤 그 레벨의 커맨드 리스트를 먼저 GPU 큐에 제출한다.
        // 그래야 레벨 N+1 이 참조할 수도 있는 레벨 N 의 출력(예: DepthPrepass → ForwardOpaque)이
        // 커맨드 기록 순서와 무관하게 GPU 타임라인에서도 먼저 끝난다(같은 큐에 대한
        // ExecuteCommandLists 호출 순서가 곧 실행 순서다). 패스 콜백이 참조하는 FrameRenderer 쪽 프레임
        // 공유 상태(예: "직전 패스가 이 리소스를 이미 클리어했는가")도 이 순서 보장 덕에 안전하다.
        // 같은 자원을 놓고 경합하는 두 패스는 compile() 의 Write-after-Write/Read-after-Write 엣지로
        // 이미 서로 다른 레벨에 배치되어 있다.
        for ( const vector<hashed_string>& level : _listCompiledLevel )
        {
            if ( recordAndSubmitLevel( context, level, pTaskManager, pDevice ) == false )
                return false;
        }
        return true;
    }

    bool RenderGraph::prepareParallelCommandLists( IRHIDevice* pDevice )
    {
        // 엔트리 목록과 노드별 커맨드 리스트는 프레임을 넘어 다시 쓴다(`ParallelScratch`, 완전한 타입은 이 파일에만 있다).
        // 패스마다 프레임마다 새로 만들면 래퍼 하나에 백엔드 할당까지 프레임당 패스 수의 몇 배가 된다.
        if ( _pParallelScratch == nullptr )
            _pParallelScratch = make_unique<ParallelScratch>();
        if ( _pParallelScratch->_pCmdListDevice != pDevice )
        {
            releaseCommandLists(); // 지난 디바이스의 리스트다. 새 디바이스에서 다시 만든다.
            _pParallelScratch->_pCmdListDevice = pDevice;
        }
        ParallelScratch& scratch = *_pParallelScratch;
        if ( scratch._listNodeCmdList.size() < _listNode.size() )
            scratch._listNodeCmdList.resize( _listNode.size() );

        // 모든 레벨의 커맨드 리스트를 **기록과 제출 전에** 마련한다. 만들 수 없으면 아무것도 내지 않고 이 프레임을 실패로 돌려준다.
        // 주의: 레벨을 돌며 만들다 실패했을 때 직렬 `execute` 로 넘어가면, 앞 레벨이 이미 기록과 제출을 마친 뒤라 그 패스들이 **두 번** 돌고
        // 직렬 경로는 커맨드 리스트 없이 기록한다(호출하는 쪽 `FrameRenderer` 가 병렬로 오기 전에 프레임 리스트를 닫아 제출했다).
        for ( const vector<hashed_string>& level : _listCompiledLevel )
        {
            for ( const hashed_string& passName : level )
            {
                const auto indexIt = _mapNameToIndex.find( passName );
                if ( indexIt == _mapNameToIndex.end() )
                {
                    SW_LOG_ERROR( "executeParallel: unknown pass in level: %#", passName.c_str() );
                    return false;
                }
                const RenderGraphNode& node = _listNode[indexIt->second];
                if ( node._bCulled || node._execute.isBound() == false )
                    continue;

                unique_ptr<IRHICommandList>& passCmd = scratch._listNodeCmdList[indexIt->second];
                if ( passCmd == nullptr )
                    passCmd = pDevice->createCommandList();
                if ( passCmd == nullptr )
                {
                    // 디바이스가 죽으면 매 프레임 여기로 온다. 같은 오류를 끝없이 되풀이하지 않는다.
                    static bool s_bCreateFailureLogged = false;
                    if ( s_bCreateFailureLogged == false )
                    {
                        s_bCreateFailureLogged = true;
                        SW_LOG_ERROR( "executeParallel: could not create a command list for pass '%#' - nothing was recorded or submitted this frame",
                                      passName.c_str() );
                    }
                    return false;
                }
            }
        }
        return true;
    }

    bool RenderGraph::recordAndSubmitLevel( RenderGraphExecutionContext& context, const vector<hashed_string>& listLevelPass, TaskManager* pTaskManager, IRHIDevice* pDevice )
    {
        ParallelScratch&           scratch       = *_pParallelScratch;
        vector<ParallelPassEntry>& listPassEntry = scratch._listPassEntry;
        listPassEntry.clear();
        listPassEntry.reserve( listLevelPass.size() );
        _listLevelBarrier.clear();
        _mapLevelBarrierIndex.clear();

        for ( const hashed_string& passName : listLevelPass )
        {
            const auto indexIt = _mapNameToIndex.find( passName );
            if ( indexIt == _mapNameToIndex.end() )
            {
                SW_LOG_ERROR( "executeParallel: unknown pass in level: %#", passName.c_str() );
                return false;
            }

            RenderGraphNode& node = _listNode[indexIt->second];
            if ( node._bCulled )
                continue;

            appendPassBarriers( context, node );

            if ( node._execute.isBound() == false )
                continue;

            // 리스트는 `prepareParallelCommandLists` 가 모두 마련했다.
            listPassEntry.push_back( ParallelPassEntry{ &node, scratch._listNodeCmdList[indexIt->second].get() } );
        }

        if ( listPassEntry.empty() )
            return true;

        // 이 레벨이 만질 자원의 배리어를 **여기서 미리**, 레벨 **첫 패스 리스트의 앞머리**에 발행한다
        // (언리얼 RDG 가 패스 리스트 앞머리에 배리어를 두는 방식이다). 판단과 기록 모두 렌더 스레드가 병렬 기록
        // 전에 끝내므로 패스 콜백은 이미 맞는 상태를 보고, 기록 중에 리소스 상태를 바꾸지 않는다. 배리어를
        // 병렬 기록 스레드가 정하던 구조는 실제로 여러 번 깨졌다. 같은 레벨의 다른 리스트는 큐 순서상 첫
        // 리스트 뒤에 실행되므로 배리어가 앞선다.
        //
        // 프레임 스트림에 기록하면 레벨마다 스트림을 잘라야 하고, 잘린 조각이 큐에 리스트 하나로 나간다
        // (DX12 에서 리스트당 제출 약 7µs).
        ParallelPassEntry& firstEntry = listPassEntry[0];
        firstEntry._pPassCmdList->beginCommandList();
        firstEntry._bAlreadyBegun = true;
        issueBarriers( firstEntry._pPassCmdList );

        // 이 구간 동안 bindless 레지스트리는 불변이어야 한다. 기록 중 등록과 해제가 일어나면
        // 읽는 쪽이 dangling 을 잡는다. 디바이스가 규칙 위반을 감시할 수 있게 알려 준다.
        pDevice->setParallelRecording( true );

        TaskStageHandle stage = pTaskManager->createStage( "RenderGraphLevel" );
        for ( ParallelPassEntry& entry : listPassEntry )
        {
            TaskHandle handle = pTaskManager->emplaceTask( "RenderPassRecord", SW_DELEGATE_METHOD( TaskDelegate, &ParallelPassEntry::record, &entry ) );
            if ( handle.isValid() )
            {
                // 렌더 스레드는 이 스테이지를 곧바로 기다린다. 게임 스레드의 대량 잡(트랜스폼 플러시, 씬 수집)
                // 뒤에 줄을 서면 그 줄이 그대로 프레임 지연이다. High 레인은 모든 워커가 자기 덱보다 먼저 본다.
                handle.setPriority( TaskPriority::High );
                stage.addTask( handle );
                // 레벨의 패스를 모두 넣은 뒤 한 번만 깨운다. 패스마다 깨우면 그 시그널이 기록 시간의 대부분이었다.
                pTaskManager->submitWithoutWake( handle );
            }
            else
            {
                // 작업을 넣지 못했다(풀이 바닥났거나 종료 중이다). 여기서(렌더 스레드) 직접 기록한다. 그냥 넘어가면 그 리스트가 기록 없이
                // 아래에서 제출된다. 레벨의 첫 리스트는 **열린 채로**, 나머지는 지난 프레임의 명령 그대로 제출된다.
                entry.record();
            }
        }
        pTaskManager->wakeSleepingWorkers( static_cast<uint32>( listPassEntry.size() ) );

        pTaskManager->waitStage( stage );
        pDevice->setParallelRecording( false );

        for ( ParallelPassEntry& entry : listPassEntry )
        {
            if ( entry._pPassCmdList != nullptr )
                pDevice->executeCommandList( entry._pPassCmdList );
        }
        return true;
    }

    /**
     * @brief 역방향 의존성 추적으로 최종 대상 리소스 생산에 관여하지 않는 패스를 자동 컬링합니다.
     */
    void RenderGraph::cullUnusedPasses( hashed_string targetResourceName )
    {
        cullUnreferencedPasses( { targetResourceName } );
    }

    void RenderGraph::cullUnreferencedPasses( const vector<hashed_string>& listRootOutput, vector<hashed_string>* pOutListCulledPass )
    {
        unordered_set<hashed_string> uniqueRequiredResources;
        uniqueRequiredResources.reserve( _listNode.size() * 2 );
        for ( const hashed_string& rootOut : listRootOutput )
        {
            uniqueRequiredResources.insert( rootOut );
        }

        for ( auto iter = _listNode.rbegin(); iter != _listNode.rend(); ++iter )
        {
            RenderGraphNode& node = *iter;
            bool             producesRequired{ false };
            for ( const hashed_string& output : node._listOutput )
            {
                if ( uniqueRequiredResources.find( output ) != uniqueRequiredResources.end() )
                {
                    producesRequired = true;
                    break;
                }
            }

            if ( producesRequired )
            {
                node._bCulled = false;
                for ( const hashed_string& input : node._listInput )
                {
                    uniqueRequiredResources.insert( input );
                }
            }
            else
            {
                node._bCulled = true;
                if ( pOutListCulledPass != nullptr )
                    pOutListCulledPass->push_back( node._name );
            }
        }

        (void)compile(); // 실패(순환)는 compile 이 패스 이름으로 알리고 실행 목록이 빈다.
    }

    /**
     * @brief 지정한 패스가 컬링되었는지 반환합니다.
     */
    bool RenderGraph::isPassCulled( hashed_string passName ) const
    {
        for ( const RenderGraphNode& node : _listNode )
        {
            if ( node._name == passName )
                return node._bCulled;
        }
        return false;
    }

    /**
     * @brief 그래프 구성을 Mermaid 다이어그램 텍스트로 내보냅니다.
     */
    string RenderGraph::describeCompiledOrder() const
    {
        if ( _listCompiledLevel.empty() )
            return "render graph: not compiled (or the last compile failed)\n";

        StringBuilder<constant::kMaxBuffer8192> out;
        out.appendFormat( "render graph: %# passes in %# levels (passes in one level record in parallel)\n",
                          static_cast<uint32>( _listCompiledExecutionOrder.size() ), static_cast<uint32>( _listCompiledLevel.size() ) );
        const auto appendNames = [&out]( const vector<hashed_string>& listName )
        {
            for ( size_t nameIndex = 0; nameIndex < listName.size(); ++nameIndex )
            {
                out.appendFormat( "%#%#", nameIndex == 0 ? "" : ", ", listName[nameIndex].c_str() );
            }
        };
        for ( size_t levelIndex = 0; levelIndex < _listCompiledLevel.size(); ++levelIndex )
        {
            out.appendFormat( "  level %#\n", static_cast<uint32>( levelIndex ) );
            for ( const hashed_string& passName : _listCompiledLevel[levelIndex] )
            {
                const auto nodeIt = std::find_if( _listNode.begin(), _listNode.end(), [&passName]( const RenderGraphNode& node )
                { return node._name == passName; } );
                out.appendFormat( "    %#", passName.c_str() );
                if ( nodeIt == _listNode.end() )
                {
                    out.append( "\n" );
                    continue;
                }
                out.append( "  reads [" );
                appendNames( nodeIt->_listInput );
                out.append( "] writes [" );
                appendNames( nodeIt->_listOutput );
                out.append( "]\n" );
            }
        }
        // 컬링된 패스도 적는다. "왜 이 패스가 안 도나" 의 답이다.
        for ( const RenderGraphNode& node : _listNode )
        {
            if ( node._bCulled )
                out.appendFormat( "  culled: %# (nothing reads what it writes)\n", node._name.c_str() );
        }
        return string( out.c_str() );
    }

    string RenderGraph::exportToMermaid() const
    {
        string result = "graph TD\n";
        result.reserve( _listNode.size() * 128 );
        for ( const RenderGraphNode& node : _listNode )
        {
            string passLabel = node._name.c_str();
            if ( node._bCulled )
                passLabel += " (Culled)";

            for ( const hashed_string& input : node._listInput )
            {
                result += "    " + string( input.c_str() ) + " --> " + passLabel + "\n";
            }
            for ( const hashed_string& output : node._listOutput )
            {
                result += "    " + passLabel + " --> " + string( output.c_str() ) + "\n";
            }
        }
        return result;
    }

    /**
     * @brief 그래프 의존 관계를 Graphviz DOT 다이어그램 서식으로 내보냅니다.
     */
    string RenderGraph::exportToDot() const
    {
        string result = "digraph RenderGraph {\n";
        result.reserve( _listNode.size() * 128 );
        for ( const RenderGraphNode& node : _listNode )
        {
            string passName = node._name.c_str();
            if ( node._bCulled )
                result += "    \"" + passName + "\" [style=dashed, color=gray];\n";

            for ( const hashed_string& input : node._listInput )
            {
                result += "    \"" + string( input.c_str() ) + "\" -> \"" + passName + "\";\n";
            }
            for ( const hashed_string& output : node._listOutput )
            {
                result += "    \"" + passName + "\" -> \"" + string( output.c_str() ) + "\";\n";
            }
        }
        result += "}\n";
        return result;
    }

    void RenderGraph::appendPassBarriers( RenderGraphExecutionContext& context, const RenderGraphNode& node )
    {
        for ( const hashed_string& input : node._listInput )
        {
            requestBarrier( context, input, RenderGraphResourceState::Read );
        }
        for ( const hashed_string& output : node._listOutput )
        {
            requestBarrier( context, output, RenderGraphResourceState::Write );
        }
    }

    void RenderGraph::requestBarrier( RenderGraphExecutionContext& context, hashed_string resource, RenderGraphResourceState desired )
    {
        // 같은 자원을 여러 패스가 요구하면 **한 번만** 낸다(SceneDepth 처럼 여러 패스가 읽는 자원이 레벨마다 읽기 전이를
        // 여러 번 받지 않게 한다).
        const RenderGraphResourceState before = context.transitionTo( resource, desired );
        if ( before == desired )
            return; // 이미 그 상태다. 낼 배리어가 없다.

        const auto it = _mapLevelBarrierIndex.find( resource );
        if ( it != _mapLevelBarrierIndex.end() )
        {
            // 같은 레벨에서 읽기와 쓰기를 함께 요구하는 일은 compile() 이 갈라 놓아 생기지 않는다.
            // 그래도 들어오면 더 강한 쪽(쓰기)을 남긴다.
            if ( desired == RenderGraphResourceState::Write )
                _listLevelBarrier[it->second]._after = desired;
            return;
        }

        RenderGraphBarrier barrier{};
        barrier._resource = resource;
        barrier._before   = before;
        barrier._after    = desired;
        _mapLevelBarrierIndex.emplace( resource, _listLevelBarrier.size() );
        _listLevelBarrier.push_back( barrier );
    }

    void RenderGraph::issueBarriers( IRHICommandList* pCmdList )
    {
        // 커맨드 리스트가 없어도 콜백은 부른다. 기록할 수 있는지는 받는 쪽의 사정이고, 그래프의 일은
        // "무엇을 바꿔야 하는가" 를 내는 데까지다. GPU 없는 테스트가 추론만 따로 볼 수 있게 하는 이유이기도 하다.
        if ( _listLevelBarrier.empty() || _levelPrologue.isBound() == false )
            return;

        RenderGraphLevelContext levelContext;
        levelContext._pListBarrier = &_listLevelBarrier;
        levelContext._pCmdList     = pCmdList;
        _levelPrologue( levelContext );
    }

    void RenderGraph::touchResourceLifetime( hashed_string resource, size_t passIndex, bool bWritten )
    {
        const auto it = _mapResourceLifetimeIndex.find( resource );
        if ( it != _mapResourceLifetimeIndex.end() )
        {
            RenderGraphResourceLifetime& life = _listResourceLifetime[it->second];
            life._lastPassIndex               = passIndex;
            if ( bWritten )
                life._bWritten = true;
            else
                life._bRead = true;
            return;
        }

        RenderGraphResourceLifetime life{};
        life._name           = resource;
        life._firstPassIndex = passIndex;
        life._lastPassIndex  = passIndex;
        life._bWritten       = bWritten;
        life._bRead          = bWritten == false;
        _mapResourceLifetimeIndex.emplace( resource, _listResourceLifetime.size() );
        _listResourceLifetime.push_back( life );
    }

    void RenderGraph::computeResourceLifetimes()
    {
        _listResourceLifetime.clear();
        _mapResourceLifetimeIndex.clear();

        for ( size_t passIndex = 0; passIndex < _listCompiledExecutionOrder.size(); ++passIndex )
        {
            const auto it = _mapNameToIndex.find( _listCompiledExecutionOrder[passIndex] );
            if ( it == _mapNameToIndex.end() )
                continue;
            const RenderGraphNode& node = _listNode[it->second];
            for ( const hashed_string& input : node._listInput )
            {
                touchResourceLifetime( input, passIndex, false );
            }
            for ( const hashed_string& output : node._listOutput )
            {
                touchResourceLifetime( output, passIndex, true );
            }
        }

        // 아무도 쓰지 않은 것을 읽는 패스는 **지난 프레임 내용이나 0** 을 읽는다. 이미지는 그럴듯하게 나오고
        // 로그는 조용하다. 파이프라인 XML 과 코드가 어긋났을 때 실제로 이렇게 조용히 틀렸다.
        for ( const RenderGraphResourceLifetime& life : _listResourceLifetime )
        {
            if ( life._bRead && life._bWritten == false )
            {
                SW_LOG_WARNING( "Resource '%#' is read but never written by any pass — reads stale or zeroed content.",
                                life._name.c_str() );
            }
        }
    }

    const RenderGraphResourceLifetime* RenderGraph::findResourceLifetime( hashed_string name ) const
    {
        const auto it = _mapResourceLifetimeIndex.find( name );
        return ( it != _mapResourceLifetimeIndex.end() ) ? &_listResourceLifetime[it->second] : nullptr;
    }

    /**
     * @brief 모든 패스와 컴파일 상태를 초기화합니다.
     */
    void RenderGraph::clear()
    {
        releaseCommandLists();
        _listNode.clear();
        _listCompiledExecutionOrder.clear();
        _listCompiledLevel.clear();
        _mapNameToIndex.clear();
        _listResourceLifetime.clear();
        _mapResourceLifetimeIndex.clear();
    }
} // namespace sw
