#include "pch.h"

#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    namespace
    {
        struct RenderGraphInternal
        {
            static void recordRenderPassTask( const TaskArgs& args )
            {
                RenderGraphNode* pNode    = args.get<RenderGraphNode*>( 0 );
                IRHICommandList* pCmdList = args.get<IRHICommandList*>( 1 );
                if ( pNode == nullptr || pCmdList == nullptr || pNode->_execute.isBound() == false )
                    return;

                pCmdList->beginCommandList();
                RenderGraphPassContext ctx;
                ctx._passName     = pNode->_name;
                ctx._pListInputs  = &pNode->_listInput;
                ctx._pListOutputs = &pNode->_listOutput;
                ctx._pCmdList     = pCmdList;
                pNode->_execute( ctx );
                pCmdList->endCommandList();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "RenderGraph" );

    /**
     * @brief 모든 노드 및 실행 순서 초기화
     */
    void RenderGraph::addPass( hashed_string passName, vector<hashed_string> listInput, vector<hashed_string> listOutput,
                               RenderGraphPassExecuteFn execute )
    {
        RenderGraphNode node;
        node._name       = passName;
        node._listInput  = std::move( listInput );
        node._listOutput = std::move( listOutput );
        node._execute    = std::move( execute );
        node._bCulled    = false;
        _listNode.push_back( std::move( node ) );
    }

    /**
     * @brief 그래프에 새 패스 노드 등록
     */
    bool RenderGraph::compile()
    {
        _listCompiledExecutionOrder.clear();
        _listCompiledWave.clear();

        if ( _listNode.empty() )
            return false;

        const size_t nodeCount = _listNode.size();

        // Active (non-culled) node indices
        vector<size_t> listActiveIndex;
        listActiveIndex.reserve( nodeCount );
        for ( size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            if ( _listNode[nodeIndex]._bCulled == false )
                listActiveIndex.push_back( nodeIndex );
        }

        if ( listActiveIndex.empty() )
            return false;

        // 1. Collect all writer passes for each resource in active registration order
        unordered_map<hashed_string, vector<size_t>> mapResourceWriter;
        for ( size_t nodeIndex : listActiveIndex )
        {
            for ( const hashed_string& output : _listNode[nodeIndex]._listOutput )
            {
                mapResourceWriter[output].push_back( nodeIndex );
            }
        }

        // Adjacency: producer → consumers; in-degree over active nodes
        unordered_map<size_t, vector<size_t>> adjacency;
        unordered_map<size_t, uint32>         mapInDegree;
        adjacency.reserve( listActiveIndex.size() );
        mapInDegree.reserve( listActiveIndex.size() );

        for ( size_t nodeIndex : listActiveIndex )
        {
            mapInDegree[nodeIndex] = 0;
        }

        auto addEdge = [&]( size_t from, size_t to )
        {
            if ( from == to )
                return;
            auto& listAdj = adjacency[from];
            if ( std::find( listAdj.begin(), listAdj.end(), to ) == listAdj.end() )
            {
                listAdj.push_back( to );
                ++mapInDegree[to];
            }
        };

        // 2. Chain consecutive writers of the same resource (Write-after-Write order)
        for ( const auto& [resource, listWriter] : mapResourceWriter )
        {
            for ( size_t writerIndex = 0; writerIndex + 1 < listWriter.size(); ++writerIndex )
            {
                addEdge( listWriter[writerIndex], listWriter[writerIndex + 1] );
            }
        }

        // 3. For each consumer reading an input, find the matching producer
        for ( size_t consumerIndex : listActiveIndex )
        {
            for ( const hashed_string& input : _listNode[consumerIndex]._listInput )
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

                addEdge( producerIndex, consumerIndex );
            }
        }

        std::queue<size_t> queueReady;
        for ( size_t nodeIndex : listActiveIndex )
        {
            if ( mapInDegree[nodeIndex] == 0 )
                queueReady.push( nodeIndex );
        }

        _listCompiledExecutionOrder.reserve( listActiveIndex.size() );
        // Kahn 위상 정렬을 BFS 레벨(웨이브) 단위로 배치 처리한다 — 같은 웨이브에 들어온 노드들은
        // 서로 입출력 의존이 없어(동시에 in-degree 0이 됨) 안전하게 병렬 기록할 수 있다.
        while ( queueReady.empty() == false )
        {
            const size_t           waveSize = queueReady.size();
            vector<hashed_string>& wave     = _listCompiledWave.emplace_back();
            wave.reserve( waveSize );

            for ( size_t waveSlot = 0; waveSlot < waveSize; ++waveSlot )
            {
                const size_t nodeIndex = queueReady.front();
                queueReady.pop();
                _listCompiledExecutionOrder.push_back( _listNode[nodeIndex]._name );
                wave.push_back( _listNode[nodeIndex]._name );

                unordered_map<size_t, vector<size_t>>::iterator adjIt = adjacency.find( nodeIndex );
                if ( adjIt == adjacency.end() )
                    continue;

                for ( size_t consumerIndex : adjIt->second )
                {
                    uint32& degree = mapInDegree[consumerIndex];
                    if ( degree > 0 )
                        --degree;
                    if ( degree == 0 )
                        queueReady.push( consumerIndex );
                }
            }
        }

        if ( _listCompiledExecutionOrder.size() != listActiveIndex.size() )
        {
            SW_LOG_WARNING( "Cycle detected during compile — %#/%# active passes scheduled.",
                            static_cast<uint32>( _listCompiledExecutionOrder.size() ),
                            static_cast<uint32>( listActiveIndex.size() ) );
            _listCompiledExecutionOrder.clear();
            _listCompiledWave.clear();
            return false;
        }

        _mapNameToIndex.clear();
        _mapNameToIndex.reserve( _listNode.size() );
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            _mapNameToIndex[_listNode[nodeIndex]._name] = nodeIndex;
        }

        return true;
    }

    /**
     * @brief 그래프 컴파일: 리소스 의존성 기준 Kahn 위상 정렬로 실행 시퀀스 구축
     */
    bool RenderGraph::execute( RenderGraphExecutionContext& context )
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

            for ( const hashed_string& input : node._listInput )
            {
                context.transitionTo( input, RenderGraphResourceState::Read );
            }
            for ( const hashed_string& output : node._listOutput )
            {
                context.transitionTo( output, RenderGraphResourceState::Write );
            }

            if ( node._execute.isBound() )
            {
                RenderGraphPassContext ctx;
                ctx._passName     = node._name;
                ctx._pListInputs  = &node._listInput;
                ctx._pListOutputs = &node._listOutput;
                node._execute( ctx );
            }
        }

        return true;
    }

    bool RenderGraph::executeParallel( RenderGraphExecutionContext& context, TaskManager* pTaskManager, IRHIDevice* pDevice )
    {
        if ( _listCompiledExecutionOrder.empty() && compile() == false )
            return false;

        if ( pTaskManager == nullptr || pDevice == nullptr || _listCompiledExecutionOrder.size() <= 1 ||
             pDevice->getCapabilities()._bParallelCommandRecording == 0 )
        {
            return execute( context );
        }

        context.reset();

        struct ParallelPassEntry
        {
            RenderGraphNode*            _pNode{ nullptr };
            unique_ptr<IRHICommandList> _pPassCmdList{ nullptr };
        };

        // 웨이브(의존성 레벨) 단위로 처리한다 — 같은 웨이브의 패스들만 동시에 병렬 기록하고,
        // 웨이브 경계마다 태스크를 기다린 뒤 그 웨이브의 커맨드리스트를 먼저 GPU 큐에 제출한다.
        // 그래야 웨이브 N+1이 참조할 수도 있는 웨이브 N의 출력(예: DepthPrepass → ForwardOpaque)이
        // 커맨드 기록 순서와 무관하게 GPU 타임라인에서도 먼저 끝난다(같은 큐에 대한
        // ExecuteCommandLists 호출 순서 = 실행 순서). 패스 콜백이 참조하는 FrameRenderer 쪽 프레임
        // 공유 상태(예: "직전 패스가 이 리소스를 이미 클리어했는가")도 이 순서 보장 덕에 안전하다 —
        // 같은 자원을 놓고 경합하는 두 패스는 compile()의 Write-after-Write/Read-after-Write 엣지로
        // 이미 서로 다른 웨이브에 배치되어 있다.
        for ( const vector<hashed_string>& wave : _listCompiledWave )
        {
            vector<ParallelPassEntry> listPassEntry;
            listPassEntry.reserve( wave.size() );

            for ( const hashed_string& passName : wave )
            {
                const auto indexIt = _mapNameToIndex.find( passName );
                if ( indexIt == _mapNameToIndex.end() )
                {
                    SW_LOG_ERROR( "executeParallel: unknown pass in wave: %#", passName.c_str() );
                    return false;
                }

                RenderGraphNode& node = _listNode[indexIt->second];
                if ( node._bCulled )
                    continue;

                for ( const hashed_string& input : node._listInput )
                {
                    context.transitionTo( input, RenderGraphResourceState::Read );
                }
                for ( const hashed_string& output : node._listOutput )
                {
                    context.transitionTo( output, RenderGraphResourceState::Write );
                }

                if ( node._execute.isBound() == false )
                    continue;

                unique_ptr<IRHICommandList> passCmd = pDevice->createCommandList();
                if ( passCmd == nullptr )
                {
                    // 디바이스가 죽으면 매 프레임 여기로 떨어지므로, 같은 경고를 무한 반복하지 않는다.
                    static bool s_bFallbackLogged = false;
                    if ( s_bFallbackLogged == false )
                    {
                        s_bFallbackLogged = true;
                        SW_LOG_WARNING( "Deferred command list unsupported by RHI — falling back to serial execute" );
                    }
                    return execute( context );
                }
                listPassEntry.push_back( ParallelPassEntry{ &node, std::move( passCmd ) } );
            }

            if ( listPassEntry.empty() )
                continue;

            TaskStageHandle stage = pTaskManager->createAnonymousStage( "RenderPassStage" );

            for ( ParallelPassEntry& entry : listPassEntry )
            {
                RenderGraphNode* pNode    = entry._pNode;
                IRHICommandList* pCmdList = entry._pPassCmdList.get();

                TaskHandle handle = pTaskManager->emplaceTask(
                    "RenderPassRecord",
                    SW_DELEGATE_FUNCTION( TaskArgsDelegate, RenderGraphInternal::recordRenderPassTask ),
                    MakeTaskArgs( pNode, pCmdList ) );

                if ( handle.isValid() )
                {
                    stage.addTask( handle );
                    handle.submit();
                }
            }

            pTaskManager->waitStage( stage );

            for ( ParallelPassEntry& entry : listPassEntry )
            {
                if ( entry._pPassCmdList != nullptr )
                {
                    pDevice->executeCommandList( entry._pPassCmdList.get() );
                }
            }
        }

        return true;
    }

    /**
     * @brief 컴파일된 위상 순서로 패스 콜백 실행 + 논리 리소스 전이 추적
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

        compile();
    }

    /**
     * @brief 역방향 종속성 추적을 통해 최종 Target 리소스 생산에 관여하지 않는 패스를 자동 컬링
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
     * @brief 지정된 패스가 컬링되었는지 여부 확인
     */
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
     * @brief Render Graph 의존 관계를 Graphviz DOT 다이어그램 서식으로 출력
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

    /**
     * @brief Transient Resource Aliasing을 위한 각 리소스별 수명 주기(First ~ Last Pass Index) 계산
     */
    vector<RenderGraphResourceLifetime> RenderGraph::computeResourceLifetimes() const
    {
        unordered_map<hashed_string, RenderGraphResourceLifetime> mapLifetime;

        for ( size_t passIndex = 0; passIndex < _listCompiledExecutionOrder.size(); ++passIndex )
        {
            const hashed_string passName = _listCompiledExecutionOrder[passIndex];
            auto                it       = _mapNameToIndex.find( passName );
            if ( it == _mapNameToIndex.end() )
                continue;

            const RenderGraphNode& node = _listNode[it->second];
            for ( const hashed_string& input : node._listInput )
            {
                auto lifeIt = mapLifetime.find( input );
                if ( lifeIt == mapLifetime.end() )
                {
                    RenderGraphResourceLifetime life{};
                    life._name           = input;
                    life._firstPassIndex = passIndex;
                    life._lastPassIndex  = passIndex;
                    life._bRead          = true;
                    mapLifetime[input]   = life;
                }
                else
                {
                    lifeIt->second._lastPassIndex = passIndex;
                    lifeIt->second._bRead         = true;
                }
            }

            for ( const hashed_string& output : node._listOutput )
            {
                auto lifeIt = mapLifetime.find( output );
                if ( lifeIt == mapLifetime.end() )
                {
                    RenderGraphResourceLifetime life{};
                    life._name           = output;
                    life._firstPassIndex = passIndex;
                    life._lastPassIndex  = passIndex;
                    life._bWritten       = true;
                    mapLifetime[output]  = life;
                }
                else
                {
                    lifeIt->second._lastPassIndex = passIndex;
                    lifeIt->second._bWritten      = true;
                }
            }
        }

        vector<RenderGraphResourceLifetime> listResult;
        listResult.reserve( mapLifetime.size() );
        for ( auto& [name, life] : mapLifetime )
        {
            listResult.push_back( std::move( life ) );
        }
        return listResult;
    }

    /**
     * @brief 모든 패스 및 컴파일 상태 초기화
     */
    void RenderGraph::clear()
    {
        _listNode.clear();
        _listCompiledExecutionOrder.clear();
        _listCompiledWave.clear();
        _mapNameToIndex.clear();
    }
} // namespace sw
