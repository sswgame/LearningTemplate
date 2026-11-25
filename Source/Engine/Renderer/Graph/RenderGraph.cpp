#include "pch.h"

#include "Engine/Renderer/Graph/RenderGraph.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/EnumUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Graph/FrameBufferTable.h"

#include <algorithm>
#include <atomic>
#include <queue>

namespace sw
{
    namespace
    {
        /// 계획 캐시의 상한입니다. 뷰 종류 · 켠 효과 조합이 이보다 많이 오가면 전체 계획만 남기고 다시 만든다.
        constexpr size_t kMaxPlanCount = 64;
        /// 이보다 패스가 적은 레벨은 병렬로 기록하지 않고 이어 기록한다 — 패스 하나짜리 레벨은 나눌 일이 없다(언리얼 RDG `r.RDG.ParallelExecute.PassMin` 도 폭을 따로 벌리지 않는다).
        constexpr size_t kMinParallelWidth = 2;

        /** @brief 패스 하나를 열린 리스트 @p pCmdList 에 기록합니다(열고 닫지 않는다). 직렬 · 병렬 · 큐 실행이 모두 이 한 함수로 패스 문맥을 만든다. @p listAccess 는 이번 계획의 접근이다. */
        void recordPassBody( RenderGraphNode& node, const vector<RenderGraphResourceAccess>& listAccess, IRHICommandList* pCmdList )
        {
            RenderGraphPassContext passContext;
            passContext._passName    = node._name;
            passContext._payload     = node._payload;
            passContext._pListAccess = &listAccess;
            passContext.setCommandList( pCmdList );
            // 끝 접근은 선언 접근에서 시작하고 패스 안 전이만 고친다. 용량은 다시 쓴다.
            node._listExitAccess.resize( listAccess.size() );
            for ( size_t index = 0; index < listAccess.size(); ++index )
            {
                node._listExitAccess[index] = listAccess[index]._access;
            }
            passContext.setExitAccesses( &node._listExitAccess );
            node._execute( passContext );
        }

        RHIClosedCommandList recordRenderPass( RenderGraphNode* pNode, const vector<RenderGraphResourceAccess>& listAccess, IRHICommandList* pCmdList, bool bAlreadyBegun )
        {
            const bool bNodeNotRecordable = pNode == nullptr || pCmdList == nullptr || pNode->_execute.isBound() == false;
            if ( bNodeNotRecordable )
                return pCmdList != nullptr && bAlreadyBegun ? pCmdList->closeCommandList() : RHIClosedCommandList{}; // 레벨 배리어는 이미 이 리스트에 있다

            // 레벨의 첫 리스트는 렌더 스레드가 이미 열어 배리어를 앞머리에 기록해 뒀다. 이어서 기록한다.
            if ( bAlreadyBegun == false )
                pCmdList->beginCommandList();
            recordPassBody( *pNode, listAccess, pCmdList );
            return pCmdList->closeCommandList();
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
            static void collectDependencyEdges( const vector<RenderGraphNode>& listNode, const vector<size_t>& listActiveIndex,
                                                const unordered_map<hashed_string, RenderGraphResourceKind>& mapResourceKind, DependencyGraph& outGraph )
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
                        bool        bWrittenAhead = false;
                        for ( size_t writerIndex : listWriter )
                        {
                            if ( writerIndex < consumerIndex )
                            {
                                producerIndex = writerIndex;
                                bWrittenAhead = true;
                            }
                        }
                        // 프레임을 넘는 자원을 앞서 쓰는 패스 없이 읽으면 지난 프레임 값을 읽는 것이다(히스토리) — 이번 프레임의 첫 쓰기보다 앞에 와야 한다(WAR).
                        // 첫 쓰기를 생산자로 잡으면 "읽고 → 쓰는" 히스토리가 순환이 된다.
                        const auto kindIt = mapResourceKind.find( input );
                        if ( bWrittenAhead == false && kindIt != mapResourceKind.end() && kindIt->second == RenderGraphResourceKind::Persistent )
                        {
                            outGraph.addEdge( consumerIndex, listWriter.front() );
                            continue;
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
                StringBuilder<constant::buffer::kMax4096> waits;
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
    /**
     * @brief 병렬 기록 한 레벨의 패스와 그것을 기록할 리스트입니다. 리스트는 노드가 가지고 있는 것을 빌립니다(소유하지 않습니다).
     * @details 태스크는 이 엔트리의 메서드에 묶인다(`record`). 메서드 델리게이트는 포인터 둘이라 힙도 노드 팽창도 없다.
     *          주의: `MakeTaskArgs` 로 인자를 실으면 패스마다 프레임마다 힙 할당이 생기고, 인자를 노드 안에 인라인으로 넣으면 노드가 두 배로
     *          부풀어 렌더 그래프 기록이 122µs 에서 196µs 로 느려진다.
     *          엔트리 목록은 태스크를 넣기 전에 모두 채우므로 기록 중에 옮겨지지 않는다.
     */
    struct ParallelPassEntry
    {
        RenderGraphNode*                         _pNode{ nullptr };
        IRHICommandList*                         _pPassCmdList{ nullptr };
        const vector<RenderGraphResourceAccess>* _pListAccess{ nullptr }; ///< 이번 계획의 접근
        /// @brief 렌더 스레드가 이미 열고 레벨 배리어를 기록한 리스트인지 나타냅니다(레벨의 첫 엔트리입니다).
        bool                 _bAlreadyBegun{ false };
        RHIClosedCommandList _closed{}; ///< 기록을 닫은 값(기록하지 못했으면 빈 값)

        void record() { _closed = recordRenderPass( _pNode, *_pListAccess, _pPassCmdList, _bAlreadyBegun ); }
    };
} // namespace sw

namespace sw
{
    struct RenderGraph::ParallelScratch
    {
        vector<ParallelPassEntry> _listPassEntry;
        /// @brief 노드 인덱스에서 그 패스가 프레임마다 다시 여는 커맨드 리스트를 찾는 목록입니다. 처음 쓸 때 만들고 그 뒤로 다시 씁니다.
        vector<unique_ptr<IRHICommandList>> _listNodeCmdList;
        /// @brief 마지막 레벨이 병렬이라 이어 기록하던 리스트가 없을 때, 들인 자원을 되돌리는 전이를 기록하는 리스트입니다.
        unique_ptr<IRHICommandList> _pReturnCmdList;
        /// @brief 리스트를 만든 디바이스입니다. 다른 디바이스가 오면 먼저 해제합니다(백엔드 교체).
        IRHIDevice* _pCmdListDevice{ nullptr };
    };

    struct RenderGraph::BufferPool
    {
        /// @brief 이만큼 이어서 쓰이지 않은 칸은 놓는다(`allocateBuffers` 횟수 — 첨부 풀의 쉰 프레임과 같은 뜻).
        static constexpr uint32 kIdleRunsBeforeRelease = 120;

        struct Request
        {
            hashed_string         _name;
            RenderGraphBufferDesc _desc;
            uint32                _firstLevel;
            uint32                _lastLevel;
        };
        struct Slot
        {
            RHIStructuredBuffer _buffer;
            RHIBufferUsage      _usage;
            uint32              _elementSize;
            uint32              _lastLevel; ///< 이번 실행에서 이 칸을 쓴 마지막 레벨(쓰지 않았으면 무효)
            uint32              _idleRuns;
        };

        vector<Request> _listRequest;
        vector<Slot>    _listSlot;
        IRHIDevice*     _pDevice{ nullptr }; ///< 칸을 만든 디바이스

        /** @brief 레벨 구간이 겹치지 않는 같은 모양의 칸입니다 — 이미 충분히 큰 가장 작은 칸, 없으면 이번 실행이 아직 쓰지 않아 키울 수 있는 칸, 그것도 없으면 새 칸. */
        Slot& acquireSlot( const Request& request )
        {
            Slot* pReuse = nullptr;
            for ( Slot& slot : _listSlot )
            {
                const bool bUnused = slot._lastLevel == constant::invalid_index::kUint32;
                const bool bFree   = bUnused || slot._lastLevel < request._firstLevel;
                const bool bFits   = slot._buffer._capacityElements >= request._desc._elementCount;
                if ( slot._usage != request._desc._usage || slot._elementSize != request._desc._elementSize || bFree == false || ( bFits == false && bUnused == false ) )
                    continue;
                const bool bBetter = pReuse == nullptr || ( bFits && ( pReuse->_buffer._capacityElements < request._desc._elementCount || slot._buffer._capacityElements < pReuse->_buffer._capacityElements ) );
                if ( bBetter )
                    pReuse = &slot;
            }
            if ( pReuse == nullptr )
            {
                _listSlot.push_back( Slot{ RHIStructuredBuffer{}, request._desc._usage, request._desc._elementSize, constant::invalid_index::kUint32, 0 } );
                pReuse = &_listSlot.back();
            }
            pReuse->_lastLevel = request._lastLevel;
            pReuse->_idleRuns  = 0;
            return *pReuse;
        }
    };

    RenderGraph::RenderGraph()
        : _pParallelScratch{ make_unique<ParallelScratch>() }
        , _pBufferPool{ make_unique<BufferPool>() }
    {
    }

    RenderGraph::~RenderGraph()                                   = default;
    RenderGraph::RenderGraph( RenderGraph&& ) noexcept            = default;
    RenderGraph& RenderGraph::operator=( RenderGraph&& ) noexcept = default;

    void RenderGraph::releaseDeviceResources()
    {
        if ( _pParallelScratch == nullptr )
            return;
        _pParallelScratch->_listNodeCmdList.clear();
        _pParallelScratch->_pReturnCmdList.reset();
        _pParallelScratch->_listPassEntry.clear();
        _pParallelScratch->_pCmdListDevice = nullptr;
        for ( BufferPool::Slot& slot : _pBufferPool->_listSlot )
        {
            slot._buffer.release( _pBufferPool->_pDevice );
        }
        _pBufferPool->_listSlot.clear();
        _pBufferPool->_listRequest.clear();
        _pBufferPool->_pDevice = nullptr;
    }

    optional<RHIBufferState> RenderGraphAccessUtil::findBufferState( RenderGraphAccess access )
    {
        switch ( access )
        {
            case RenderGraphAccess::ShaderRead:
                return RHIBufferState::ShaderResource;
            case RenderGraphAccess::UnorderedAccess:
            case RenderGraphAccess::UnorderedAccessRead:
                return RHIBufferState::UnorderedAccess;
            case RenderGraphAccess::IndirectArgs:
                return RHIBufferState::IndirectArgument;
            case RenderGraphAccess::CopySource:
                return RHIBufferState::CopySource;
            case RenderGraphAccess::CopyDest:
                return RHIBufferState::CopyDest;
            case RenderGraphAccess::RenderTarget:
            case RenderGraphAccess::DepthRead:
            case RenderGraphAccess::DepthWrite:
                break;
        }
        return nullopt;
    }

    optional<RHITextureState> RenderGraphAccessUtil::findTextureState( RenderGraphAccess access )
    {
        switch ( access )
        {
            case RenderGraphAccess::ShaderRead:
            case RenderGraphAccess::DepthRead:
                return RHITextureState::ShaderResource;
            case RenderGraphAccess::UnorderedAccess:
            case RenderGraphAccess::UnorderedAccessRead:
                return RHITextureState::UnorderedAccess;
            case RenderGraphAccess::RenderTarget:
            case RenderGraphAccess::DepthWrite:
                return RHITextureState::RenderTarget;
            case RenderGraphAccess::IndirectArgs:
            case RenderGraphAccess::CopySource:
            case RenderGraphAccess::CopyDest:
                break;
        }
        return nullopt;
    }

    bool RenderGraphPassContext::isDeclared( const RenderGraphResourceAccess& access ) const
    {
        if ( _pListAccess == nullptr )
            return false;
        for ( const RenderGraphResourceAccess& declared : *_pListAccess )
        {
            if ( declared._resource == access._resource && declared._access == access._access && declared._viewable == access._viewable )
                return true;
        }
        return false;
    }

    uint64 RenderGraphPassContext::findRegisteredHandle( const RenderGraphResourceAccess& access, uint32 slot ) const
    {
        if ( _pResourceTable == nullptr )
            return 0;
        return access._viewable == RenderGraphViewableKind::Buffer ? _pResourceTable->findBuffer( access._resource, slot ).getPacked() : _pResourceTable->findTexture( access._resource, slot ).getPacked();
    }

    uint64 RenderGraphPassContext::findDeclaredHandle( const RenderGraphResourceAccess& access, uint32 slot ) const
    {
        if ( isDeclared( access ) == false )
        {
            // 선언하지 않은 접근을 주면 그래프가 그 전이를 내지 않은 자원을 묶는다 — 무효 핸들로 막는다(손으로 묶을 길이 없다).
            SW_LOG_ERROR_ONCE_ID( "Renderer.GraphUndeclaredAccess", "Render graph pass '%#' asked for '%#' with an access it did not declare - it gets no handle", _passName.c_str(),
                                  access._resource.c_str() );
            return 0;
        }
        const uint64 handle = findRegisteredHandle( access, slot );
        if ( handle == 0 )
            SW_LOG_ERROR_ONCE_ID( "Renderer.GraphUnregisteredResource", "Render graph pass '%#' declared '%#' but nothing registered slot %# this frame - it gets no handle", _passName.c_str(),
                                  access._resource.c_str(), slot );
        return handle;
    }

    RHIBufferSRVIndex RenderGraphPassContext::findDeclaredSRV( const RenderGraphResourceAccess& access, uint32 slot ) const
    {
        const bool                           bFound = findDeclaredHandle( access, slot ) != 0;
        const FrameBufferTable::Entry* const pEntry = bFound ? _pResourceTable->findEntry( access._resource, slot ) : nullptr;
        return pEntry != nullptr ? pEntry->_srv : kInvalidDescriptorIndex;
    }

    RHIUnorderedAccessIndex RenderGraphPassContext::getBufferUAV( RenderGraphBufferUAV handle, uint32 slot ) const
    {
        const bool                           bFound = findDeclaredHandle( handle, slot ) != 0;
        const FrameBufferTable::Entry* const pEntry = bFound ? _pResourceTable->findEntry( handle._name, slot ) : nullptr;
        return pEntry != nullptr ? pEntry->_uav : kInvalidDescriptorIndex;
    }

    void RenderGraphPassContext::uavBarrier( RenderGraphBufferUAV handle, uint32 slot ) const
    {
        const RHIBufferHandle buffer{ findDeclaredHandle( handle, slot ) };
        if ( buffer != 0 && _pCmdList != nullptr )
            _pCmdList->uavBarrier( buffer );
    }

    IRHIPassCommandList* RenderGraphPassContext::getCommandList() const
    {
        return _pCmdList;
    }

    void RenderGraphPassContext::uavBarrier( RenderGraphTextureUAV handle, uint32 slot ) const
    {
        const RHITextureHandle texture{ findDeclaredHandle( handle, slot ) };
        if ( texture != 0 && _pCmdList != nullptr )
            _pCmdList->uavBarrierTexture( texture );
    }

    void RenderGraphPassContext::transitionDeclared( const RenderGraphResourceAccess& declared, RenderGraphAccess target, uint32 slot ) const
    {
        const uint64 handle = findDeclaredHandle( declared, slot );
        if ( handle == 0 || _pCmdList == nullptr )
            return;
        const bool                      bBuffer      = declared._viewable == RenderGraphViewableKind::Buffer;
        const optional<RHIBufferState>  bufferState  = bBuffer ? RenderGraphAccessUtil::findBufferState( target ) : optional<RHIBufferState>{};
        const optional<RHITextureState> textureState = bBuffer ? optional<RHITextureState>{} : RenderGraphAccessUtil::findTextureState( target );
        if ( bufferState.has_value() )
        {
            _pCmdList->transitionBuffer( RHIBufferHandle{ handle }, *bufferState );
        }
        else if ( textureState.has_value() )
        {
            const RHITextureTransition transition{ RHITextureHandle{ handle }, *textureState };
            _pCmdList->transitionTextures( &transition, 1 );
        }
        else
        {
            // 자원 종류에 없는 접근(버퍼의 첨부 접근 · 텍스처의 복사 접근)이다 — 낼 상태가 없어 추적도 바꾸지 않는다.
            SW_LOG_ERROR_ONCE_ID( "Renderer.GraphTransitionWithoutState", "Render graph pass '%#' transitions '%#' to an access this resource kind has no state for - nothing is issued", _passName.c_str(),
                                  declared._resource.c_str() );
            return;
        }
        if ( _pListExitAccess == nullptr )
            return;
        for ( size_t index = 0; index < _pListAccess->size(); ++index )
        {
            if ( ( *_pListAccess )[index]._resource == declared._resource )
                ( *_pListExitAccess )[index] = target;
        }
    }

    /**
     * @brief 그래프에 새 패스 노드를 등록합니다.
     */
    bool RenderGraph::addPass( hashed_string passName, vector<hashed_string> listInput, vector<hashed_string> listOutput,
                               RenderGraphPassExecuteFn execute, RHIQueueKind queue )
    {
        // 이름은 패스의 키다(실행 순서, 레벨, 이름에서 노드를 찾는 테이블). 같은 이름을 받으면 그 테이블이 뒤의 것으로 덮여 한 패스는 두 번,
        // 다른 하나는 한 번도 돌지 않고, 병렬 기록에서는 두 작업 스레드가 같은 커맨드 리스트에 쓴다. 그래서 거절한다.
        for ( const RenderGraphNode& existing : _listNode )
        {
            if ( existing._name == passName )
            {
                SW_LOG_ERROR_ID( "Renderer.GraphDuplicatePass", "Render graph pass '%#' is declared twice - the second declaration is ignored", passName.c_str() );
                return false;
            }
        }

        // 이름 판: 입력은 셰이더 읽기, 출력은 렌더 타깃 텍스처다(레벨 콜백이 늘 그렇게 옮겨 왔다). 같은 이름을 두 번 적은 옛 선언도 받는다.
        RenderGraphNode node;
        node._name = passName;
        node._listAccess.reserve( listInput.size() + listOutput.size() );
        for ( const hashed_string& input : listInput )
        {
            node._listAccess.push_back( RenderGraphResourceAccess{ input, RenderGraphAccess::ShaderRead, RenderGraphViewableKind::Texture } );
        }
        for ( const hashed_string& output : listOutput )
        {
            node._listAccess.push_back( RenderGraphResourceAccess{ output, RenderGraphAccess::RenderTarget, RenderGraphViewableKind::Texture } );
        }
        node._listInput  = std::move( listInput );
        node._listOutput = std::move( listOutput );
        node._execute    = std::move( execute );
        node._queue      = queue;
        _listNode.push_back( std::move( node ) );
        return true;
    }

    bool RenderGraph::addPass( hashed_string passName, vector<RenderGraphResourceAccess> listAccess, RenderGraphPassExecuteFn execute, RHIQueueKind queue, uint32 payload )
    {
        for ( size_t index = 0; index < listAccess.size(); ++index )
        {
            const RenderGraphResourceAccess& access = listAccess[index];
            if ( queue == RHIQueueKind::AsyncCompute && isRenderGraphGraphicsOnlyAccess( access._access ) )
            {
                SW_LOG_ERROR_ID( "Renderer.GraphAsyncAttachment", "Render graph pass '%#' runs on the async compute queue but declares '%#' as an attachment - the pass is ignored",
                                 passName.c_str(), access._resource.c_str() );
                return false;
            }
            for ( size_t other = 0; other < index; ++other )
            {
                if ( listAccess[other]._resource == access._resource )
                {
                    // 한 패스가 한 자원을 두 접근으로 적으면 어느 상태로 둘지 정할 수 없다 — 읽고 쓰는 자원은 UAV 하나다.
                    SW_LOG_ERROR_ID( "Renderer.GraphDuplicateAccess", "Render graph pass '%#' declares '%#' twice - the pass is ignored", passName.c_str(), access._resource.c_str() );
                    return false;
                }
            }
        }
        vector<hashed_string> listInput;
        vector<hashed_string> listOutput;
        for ( const RenderGraphResourceAccess& access : listAccess )
        {
            ( isRenderGraphWriteAccess( access._access ) ? listOutput : listInput ).push_back( access._resource );
        }
        if ( addPass( passName, std::move( listInput ), std::move( listOutput ), std::move( execute ), queue ) == false )
            return false;
        _listNode.back()._listAccess = std::move( listAccess );
        _listNode.back()._payload    = payload;
        return true;
    }

    void RenderGraph::importResource( hashed_string name, RenderGraphViewableKind type, RenderGraphAccess initialAccess )
    {
        // 파이프라인이 Persistent 로 선언한 자원(HZB · 입자 상태)은 그 종류를 지킨다 — 들이기는 처음 접근만 정한다.
        if ( getResourceKind( name ) == RenderGraphResourceKind::Transient )
            declareResource( name, RenderGraphResourceKind::External );
        for ( RenderGraphResourceAccess& imported : _listImportedResource )
        {
            if ( imported._resource == name )
            {
                imported = RenderGraphResourceAccess{ name, initialAccess, type };
                return;
            }
        }
        _listImportedResource.push_back( RenderGraphResourceAccess{ name, initialAccess, type } );
    }

    void RenderGraph::importResources( const FrameBufferTable& table )
    {
        _listImportedResource.clear();
        _pBufferPool->_listRequest.clear();
        for ( const uint32 entryIndex : table.getFirstEntries() )
        {
            const FrameBufferTable::Entry& entry = table.getEntries()[entryIndex];
            if ( entry._bGraphOwned == false )
                importResource( entry._name, entry._viewable, entry._initialAccess );
        }
    }

    void RenderGraph::createBuffer( hashed_string name, const RenderGraphBufferDesc& desc )
    {
        SW_ASSERT( getResourceKind( name ) == RenderGraphResourceKind::Transient && desc._elementSize > 0 && desc._elementCount > 0 );
        const RenderGraphResourceLifetime* pLifetime = findResourceLifetime( name );
        if ( pLifetime == nullptr )
            return;
        _pBufferPool->_listRequest.push_back( BufferPool::Request{ name, desc, pLifetime->_firstLevel, pLifetime->_lastLevel } );
    }

    void RenderGraph::allocateBuffers( IRHIDevice& device, FrameBufferTable& table )
    {
        BufferPool& pool = *_pBufferPool;
        if ( pool._pDevice != &device )
        {
            for ( BufferPool::Slot& slot : pool._listSlot )
            {
                slot._buffer.release( pool._pDevice );
            }
            pool._listSlot.clear();
            pool._pDevice = &device;
        }
        for ( BufferPool::Slot& slot : pool._listSlot )
        {
            slot._lastLevel = constant::invalid_index::kUint32;
            ++slot._idleRuns;
        }
        // 먼저 시작하는 것부터 — 앞에서 끝난 칸을 뒤의 것이 다시 쓴다.
        VectorUtil::stableSort( pool._listRequest, []( const BufferPool::Request& left, const BufferPool::Request& right )
        { return left._firstLevel < right._firstLevel; } );
        for ( const BufferPool::Request& request : pool._listRequest )
        {
            const bool        bSRV = EnumUtil::hasFlag( request._desc._usage, RHIBufferUsage::Structured );
            const bool        bUAV = EnumUtil::hasFlag( request._desc._usage, RHIBufferUsage::UnorderedAccess );
            BufferPool::Slot& slot = pool.acquireSlot( request );
            if ( slot._buffer.ensureCapacity( &device, request._desc._elementSize, request._desc._elementCount, request._desc._usage, bSRV, bUAV, nullptr ) )
                table.registerGraphBuffer( request._name, slot._buffer );
        }
        for ( BufferPool::Slot& slot : pool._listSlot )
        {
            if ( slot._idleRuns >= BufferPool::kIdleRunsBeforeRelease )
                slot._buffer.release( &device );
        }
        VectorUtil::removeIf( pool._listSlot, []( const BufferPool::Slot& slot )
        { return slot._buffer.isValid() == false; } );
    }

    uint32 RenderGraph::getBufferSlotCount() const
    {
        return static_cast<uint32>( _pBufferPool->_listSlot.size() );
    }

    void RenderGraph::beginExecution( RenderGraphExecutionContext& context ) const
    {
        context.reset();
        for ( const RenderGraphResourceAccess& imported : _listImportedResource )
        {
            // 텍스처의 실제 레이아웃은 백엔드가 안다(새로 만든 텍스처) — 심지 않아 첫 사용이 늘 전이를 요구한다(`importResource`).
            if ( imported._viewable == RenderGraphViewableKind::Buffer )
                context.seedAccess( imported._resource, imported._access );
        }
    }

    void RenderGraph::applyExitAccesses( RenderGraphExecutionContext& context, const RenderGraphNode& node, const vector<RenderGraphResourceAccess>& listAccess )
    {
        SW_ASSERT( node._listExitAccess.size() == listAccess.size() ); // 방금 기록한 패스다(`recordPassBody`)
        for ( size_t index = 0; index < listAccess.size(); ++index )
        {
            if ( node._listExitAccess[index] != listAccess[index]._access )
                context.seedAccess( listAccess[index]._resource, node._listExitAccess[index] );
        }
    }

    bool RenderGraph::selectPlan( const vector<uint8>& listPassState )
    {
        if ( _listPlan.empty() )
            return true;
        const bool bAllRun = VectorUtil::allOf( listPassState, []( uint8 state )
        { return state == 0; } );
        if ( bAllRun )
        {
            _activePlanIndex = 0;
            return true;
        }
        for ( size_t planIndex = 1; planIndex < _listPlan.size(); ++planIndex )
        {
            if ( _listPlan[planIndex]._listPassState == listPassState )
            {
                _activePlanIndex = planIndex;
                return true;
            }
        }
        return false;
    }

    void RenderGraph::addPlan( const vector<uint8>& listPassState, const unordered_map<hashed_string, hashed_string>& mapForward )
    {
        if ( _listPlan.empty() )
            return;
        if ( _listPlan.size() >= kMaxPlanCount )
            _listPlan.resize( 1 );
        Plan plan;
        plan._listPassState = listPassState;
        // 이 레벨에서 이미 놓은 패스들의 접근 — 같은 자원을 다르게 읽거나 쓰는 패스는 다음 작은 레벨로 간다.
        vector<unordered_map<hashed_string, RenderGraphAccess>> listSubLevelAccess;
        for ( const vector<PlanPass>& listLevelPass : _listPlan[0]._listLevel )
        {
            const size_t firstSubLevel = plan._listLevel.size();
            listSubLevelAccess.clear();
            for ( const PlanPass& source : listLevelPass )
            {
                const uint32 payload = _listNode[source._nodeIndex]._payload;
                if ( payload < listPassState.size() && listPassState[payload] != 0 )
                    continue;
                PlanPass pass;
                pass._nodeIndex = source._nodeIndex;
                pass._listAccess.reserve( source._listAccess.size() );
                for ( const RenderGraphResourceAccess& access : source._listAccess )
                {
                    const bool bTextureRead = access._viewable == RenderGraphViewableKind::Texture && isRenderGraphWriteAccess( access._access ) == false;
                    if ( bTextureRead == false || mapForward.find( access._resource ) == mapForward.end() )
                        pass._listAccess.push_back( access );
                }
                // 가리키는 이름으로 바뀐 읽기는 그 뒤에 — 같은 이름의 선언(쓰기 · 같은 읽기)이 이미 있으면 그것 하나다.
                for ( const RenderGraphResourceAccess& access : source._listAccess )
                {
                    const bool bTextureRead = access._viewable == RenderGraphViewableKind::Texture && isRenderGraphWriteAccess( access._access ) == false;
                    const auto forwardIt    = mapForward.find( access._resource );
                    if ( bTextureRead == false || forwardIt == mapForward.end() || forwardIt->second.empty() )
                        continue;
                    const bool bDeclared = VectorUtil::containsIf( pass._listAccess, [&forwardIt]( const RenderGraphResourceAccess& declared )
                    { return declared._resource == forwardIt->second; } );
                    if ( bDeclared == false )
                        pass._listAccess.push_back( RenderGraphResourceAccess{ forwardIt->second, access._access, access._viewable } );
                }
                size_t subLevel = 0;
                for ( ; subLevel < listSubLevelAccess.size(); ++subLevel )
                {
                    const unordered_map<hashed_string, RenderGraphAccess>& mapAccess = listSubLevelAccess[subLevel];
                    const bool                                             bConflict = VectorUtil::containsIf( pass._listAccess, [&mapAccess]( const RenderGraphResourceAccess& access )
                                                                {
                        const auto it = mapAccess.find( access._resource );
                        return it != mapAccess.end() && ( it->second != access._access || isRenderGraphWriteAccess( access._access ) );
                    } );
                    if ( bConflict == false )
                        break;
                }
                if ( subLevel == listSubLevelAccess.size() )
                {
                    listSubLevelAccess.emplace_back();
                    plan._listLevel.emplace_back();
                }
                for ( const RenderGraphResourceAccess& access : pass._listAccess )
                {
                    listSubLevelAccess[subLevel][access._resource] = access._access;
                }
                plan._listLevel[firstSubLevel + subLevel].push_back( std::move( pass ) );
            }
        }
        _activePlanIndex = _listPlan.size();
        _listPlan.push_back( std::move( plan ) );
    }

    void RenderGraph::collectReturnBarriers( RenderGraphExecutionContext& context )
    {
        _listLevelBarrier.clear();
        _mapLevelBarrierIndex.clear();
        for ( const RenderGraphResourceAccess& imported : _listImportedResource )
        {
            const optional<RenderGraphAccess> lastAccess = context.findAccess( imported._resource );
            if ( lastAccess.has_value() && *lastAccess != imported._access )
                requestBarrier( context, imported, RHIQueueKind::Graphics );
        }
    }

    /**
     * @brief 그래프를 컴파일합니다. 리소스 의존성 기준 Kahn 위상 정렬로 실행 순서를 만듭니다.
     */
    bool RenderGraph::compile()
    {
        using Internal = RenderGraphCompileInternal;
        _listCompiledExecutionOrder.clear();
        _listCompiledLevel.clear();
        _listQueueStep.clear();
        _listFinalJoinBatch.clear();
        _asyncBatchCount = 0;
        _listPlan.clear();
        _activePlanIndex = 0;

        if ( _listNode.empty() )
            return false;

        // 활성(컬링되지 않은) 노드 인덱스
        vector<size_t> listActiveIndex;
        listActiveIndex.reserve( _listNode.size() );
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            listActiveIndex.push_back( nodeIndex );
        }
        if ( listActiveIndex.empty() )
            return false;

        Internal::DependencyGraph graph;
        Internal::collectDependencyEdges( _listNode, listActiveIndex, _mapResourceKind, graph );
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
        if ( validateLevelReadAccesses() == false )
        {
            _listCompiledExecutionOrder.clear();
            _listCompiledLevel.clear();
            return false;
        }

        // 수명은 실행 순서가 정해진 **뒤에야** 의미가 있다. "몇 번째 패스에서 처음 또는 마지막으로 쓰이나" 이기 때문이다.
        computeResourceLifetimes();
        computeQueuePlan( graph._adjacency );

        Plan fullPlan;
        fullPlan._listLevel.reserve( _listCompiledLevel.size() );
        for ( const vector<hashed_string>& listLevelPass : _listCompiledLevel )
        {
            vector<PlanPass>& listPlanPass = fullPlan._listLevel.emplace_back();
            listPlanPass.reserve( listLevelPass.size() );
            for ( const hashed_string& passName : listLevelPass )
            {
                const size_t nodeIndex = _mapNameToIndex[passName];
                listPlanPass.push_back( PlanPass{ nodeIndex, _listNode[nodeIndex]._listAccess } );
            }
        }
        _listPlan.push_back( std::move( fullPlan ) );
        return true;
    }

    bool RenderGraph::validateLevelReadAccesses() const
    {
        // 한 레벨의 배리어는 자원마다 하나다(`requestBarrier`). 같은 레벨의 두 패스가 한 자원을 다른 읽기(셰이더 읽기 · 간접 인자 · 복사 원본)로 읽으면
        // 앞 접근만 GPU 에 나가고 추적은 뒤 접근을 믿는다 — 읽기 상태를 합치는 묶음(언리얼 ERHIAccess 비트 합)이 생길 때까지 컴파일을 멈춘다.
        unordered_map<hashed_string, RenderGraphAccess> mapLevelRead;
        for ( const vector<hashed_string>& listLevelPass : _listCompiledLevel )
        {
            mapLevelRead.clear();
            for ( const hashed_string& passName : listLevelPass )
            {
                const auto indexIt = _mapNameToIndex.find( passName );
                if ( indexIt == _mapNameToIndex.end() )
                    continue;
                for ( const RenderGraphResourceAccess& access : _listNode[indexIt->second]._listAccess )
                {
                    if ( isRenderGraphWriteAccess( access._access ) )
                        continue;
                    const auto [readIt, bInserted] = mapLevelRead.emplace( access._resource, access._access );
                    if ( bInserted == false && readIt->second != access._access )
                    {
                        SW_LOG_ERROR_ID( "Renderer.GraphMixedReadAccess", "Render graph: '%#' is read two different ways in one level (pass '%#') - the level can carry one read state per resource",
                                         access._resource.c_str(), passName.c_str() );
                        return false;
                    }
                }
            }
        }
        return true;
    }

    void RenderGraph::computeQueuePlan( const unordered_map<size_t, vector<size_t>>& mapAdjacency )
    {
        _listQueueStep.assign( _listCompiledExecutionOrder.size(), RenderGraphQueueStep{} );
        _listFinalJoinBatch.clear();
        _asyncBatchCount = 0;

        // 소비자 → 생산자들(간선의 역방향). 그래픽스 패스가 기다릴 묶음은 자기 생산자가 속한 묶음이다.
        unordered_map<size_t, vector<size_t>> mapProducer;
        for ( const auto& [producerIndex, listConsumer] : mapAdjacency )
        {
            for ( size_t consumerIndex : listConsumer )
            {
                mapProducer[consumerIndex].push_back( producerIndex );
            }
        }

        constexpr uint32 kNoBatch = ~0u;
        vector<uint32>   listBatchOfNode( _listNode.size(), kNoBatch );
        vector<uint8>    listBatchJoined;
        for ( size_t orderIndex = 0; orderIndex < _listCompiledExecutionOrder.size(); ++orderIndex )
        {
            const size_t          nodeIndex = _mapNameToIndex[_listCompiledExecutionOrder[orderIndex]];
            RenderGraphQueueStep& step      = _listQueueStep[orderIndex];
            // 능력이 없으면 선언과 상관없이 그래픽스로 계획한다 — 같은 순서로 돌 뿐 결과는 같다.
            const bool bAsync = _bAsyncComputeSupported && _listNode[nodeIndex]._queue == RHIQueueKind::AsyncCompute;
            step._queue       = bAsync ? RHIQueueKind::AsyncCompute : RHIQueueKind::Graphics;
            if ( bAsync )
            {
                // 실행 순서에서 이어진 비동기 패스는 한 묶음(컴퓨트 리스트 하나)이다. 앞 패스가 그래픽스였으면 여기서 포크한다.
                const bool bPreviousAsync = orderIndex > 0 && _listQueueStep[orderIndex - 1]._queue == RHIQueueKind::AsyncCompute;
                if ( bPreviousAsync == false )
                {
                    step._bBeginsBatch = 1;
                    ++_asyncBatchCount;
                    listBatchJoined.push_back( 0 );
                }
                step._batch                = _asyncBatchCount - 1;
                listBatchOfNode[nodeIndex] = step._batch;
                continue;
            }
            const auto producerIt = mapProducer.find( nodeIndex );
            if ( producerIt == mapProducer.end() )
                continue;
            for ( size_t producerIndex : producerIt->second )
            {
                const uint32 batch = listBatchOfNode[producerIndex];
                if ( batch == kNoBatch || listBatchJoined[batch] != 0 )
                    continue;
                step._listJoinBatch.push_back( batch );
                listBatchJoined[batch] = 1;
            }
            VectorUtil::sort( step._listJoinBatch );
        }
        for ( size_t orderIndex = 0; orderIndex < _listQueueStep.size(); ++orderIndex )
        {
            const bool bAsync                      = _listQueueStep[orderIndex]._queue == RHIQueueKind::AsyncCompute;
            const bool bNextAsync                  = orderIndex + 1 < _listQueueStep.size() && _listQueueStep[orderIndex + 1]._queue == RHIQueueKind::AsyncCompute;
            _listQueueStep[orderIndex]._bEndsBatch = ( bAsync && bNextAsync == false ) ? 1 : 0;
        }
        // 그래프 밖(뷰 그래프 · 다음 프레임)이 결과를 읽는다 — 아무도 조인하지 않은 묶음은 그래프 끝에서 조인한다.
        for ( uint32 batch = 0; batch < _asyncBatchCount; ++batch )
        {
            if ( listBatchJoined[batch] == 0 )
                _listFinalJoinBatch.push_back( batch );
        }
    }

    bool RenderGraph::executeOnQueues( RenderGraphExecutionContext& context, IRHIDevice& device, IRHICommandList& graphicsList, IRHICommandList* pAsyncList )
    {
        beginExecution( context );
        if ( _listCompiledExecutionOrder.empty() && compile() == false )
            return false;
        if ( _asyncBatchCount > 0 && pAsyncList == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "RenderGraph.AsyncListMissing", "executeOnQueues: %# async compute batch(es) planned but no async command list was given - nothing recorded", _asyncBatchCount );
            return false;
        }

        vector<RHIQueueSyncPoint> listBatchPoint( _asyncBatchCount );
        // 그래픽스 리스트를 닫아 넘기고 다시 연다 — 포크의 신호와 조인의 대기는 큐에 넘긴 일에만 걸리므로 그 자리까지의 기록을 먼저 넘긴다.
        const auto handOffGraphics = [&device, &graphicsList]()
        {
            device.executeCommandList( graphicsList.closeCommandList() );
            graphicsList.beginCommandList();
        };
        const auto joinBatches = [&]( const vector<uint32>& listBatch )
        {
            if ( listBatch.empty() )
                return;
            device.executeCommandList( graphicsList.closeCommandList() );
            for ( uint32 batch : listBatch )
            {
                device.waitForAsyncCompute( listBatchPoint[batch] );
            }
            graphicsList.beginCommandList();
        };

        for ( size_t orderIndex = 0; orderIndex < _listCompiledExecutionOrder.size(); ++orderIndex )
        {
            const auto indexIt = _mapNameToIndex.find( _listCompiledExecutionOrder[orderIndex] );
            if ( indexIt == _mapNameToIndex.end() )
            {
                SW_LOG_ERROR_ID( "RenderGraph.ExecuteUnknownPassOrder", "execute: unknown pass in order: %#", _listCompiledExecutionOrder[orderIndex].c_str() );
                return false;
            }
            RenderGraphNode&            node = _listNode[indexIt->second];
            const RenderGraphQueueStep& step = _listQueueStep[orderIndex];
            if ( step._queue == RHIQueueKind::Graphics )
                joinBatches( step._listJoinBatch );
            else if ( step._bBeginsBatch != 0 )
                pAsyncList->beginCommandList();

            IRHICommandList* const pList = step._queue == RHIQueueKind::Graphics ? &graphicsList : pAsyncList;
            _listLevelBarrier.clear();
            _mapLevelBarrierIndex.clear();
            appendPassBarriers( context, node, node._listAccess );
            issueBarriers( pList );
            if ( node._execute.isBound() )
            {
                recordPassBody( node, node._listAccess, pList );
                applyExitAccesses( context, node, node._listAccess );
            }

            if ( step._queue == RHIQueueKind::AsyncCompute && step._bEndsBatch != 0 )
            {
                RHIClosedCommandList closedBatch = pAsyncList->closeCommandList();
                handOffGraphics(); // 포크 지점 — 이 묶음은 여기까지의 그래픽스 일 뒤에 시작한다
                listBatchPoint[step._batch] = device.submitAsyncCompute( std::move( closedBatch ) );
            }
        }
        joinBatches( _listFinalJoinBatch );
        collectReturnBarriers( context );
        issueBarriers( &graphicsList );
        return true;
    }

    /**
     * @brief 컴파일된 위상 순서로 패스 콜백을 실행하고 논리 리소스 전이를 추적합니다.
     */
    bool RenderGraph::execute( RenderGraphExecutionContext& context, IRHICommandList* pCmdList )
    {
        beginExecution( context );

        if ( _listCompiledExecutionOrder.empty() )
        {
            if ( compile() == false )
                return false;
        }

        for ( const vector<PlanPass>& listLevelPass : _listPlan[_activePlanIndex]._listLevel )
        {
            for ( const PlanPass& pass : listLevelPass )
            {
                RenderGraphNode& node = _listNode[pass._nodeIndex];
                // 직렬 경로도 **같은 추론**을 쓴다. 상태만 기록하고 배리어를 내지 않으면 전이가 패스 콜백 안 여기저기에서
                // 즉흥적으로 일어난다. 경로가 둘이면 한쪽에만 수정이 들어간다.
                // 배리어는 이 패스가 기록하는 것과 **같은 리스트**에 들어가야 한다(레벨처럼 앞으로 몰 수 없다).
                _listLevelBarrier.clear();
                _mapLevelBarrierIndex.clear();
                appendPassBarriers( context, node, pass._listAccess );
                issueBarriers( pCmdList );

                // 패스 문맥은 병렬 · 큐 실행과 같은 함수가 만든다 — 직렬만 리스트를 빠뜨리던 사본(`uavBarrier` · `transition` 이 조용히 무동작)을 두지 않는다.
                if ( node._execute.isBound() )
                {
                    recordPassBody( node, pass._listAccess, pCmdList );
                    applyExitAccesses( context, node, pass._listAccess );
                }
            }
        }
        collectReturnBarriers( context );
        issueBarriers( pCmdList );
        return true;
    }

    bool RenderGraph::executeParallel( RenderGraphExecutionContext& context, TaskManager& taskManager, IRHIDevice& device )
    {
        SW_PROFILE_SCOPE( "RT.Graph.executeParallel" );

        if ( _listCompiledExecutionOrder.empty() && compile() == false )
            return false;

        beginExecution( context );
        if ( prepareParallelCommandLists( device ) == false )
            return false;
        // 병렬 기록을 못 하는 디바이스는 모든 레벨을 아래 이어 기록하는 길로 낸다 — 리스트 없는 `execute` 로 빠지면 패스가 리스트를 받지 못해 조용히 그려지지 않는다.
        const bool bParallelRecording = device.getCapabilities()._bParallelCommandRecording != SW_FALSE;

        // 의존성 레벨 단위로 처리한다. 같은 레벨의 패스들만 동시에 병렬 기록하고,
        // 레벨 경계마다 태스크를 기다린 뒤 그 레벨의 커맨드 리스트를 먼저 GPU 큐에 제출한다.
        // 그래야 레벨 N+1 이 참조할 수도 있는 레벨 N 의 출력(예: DepthPrepass → ForwardOpaque)이
        // 커맨드 기록 순서와 무관하게 GPU 타임라인에서도 먼저 끝난다(같은 큐에 대한
        // ExecuteCommandLists 호출 순서가 곧 실행 순서다). 패스 콜백이 참조하는 FrameRenderer 쪽 프레임
        // 공유 상태(예: "직전 패스가 이 리소스를 이미 클리어했는가")도 이 순서 보장 덕에 안전하다.
        // 같은 자원을 놓고 경합하는 두 패스는 compile() 의 Write-after-Write/Read-after-Write 엣지로
        // 이미 서로 다른 레벨에 배치되어 있다.
        //
        // 패스 하나짜리 레벨이 이어지면 렌더 스레드가 리스트 하나에 레벨마다 배리어 → 패스 순으로 이어 기록하고 한 번 제출한다(언리얼 RDG 가 작은 패스를 한
        // 리스트에 묶는 자리). 레벨마다 리스트를 열고 · 태스크로 넘겨 기다리고 · 제출하는 고정 비용(레벨당 ~20 µs)이 패스 본문(대개 수 µs)보다 컸다 — 빈 씬
        // 14 패스에서 그래프 400 µs 중 300 µs. 같은 리스트 안의 기록 순서와 레벨마다의 배리어가 레벨 순서를 지키므로 GPU 순서는 그대로다. 패스가 둘 이상인
        // 레벨만 병렬로 기록하고, 그 앞에서 이어 기록하던 리스트를 먼저 제출한다.
        IRHICommandList* pSerialList      = nullptr;
        auto             submitSerialList = [&device, &pSerialList]()
        {
            if ( pSerialList == nullptr )
                return;
            device.executeCommandList( pSerialList->closeCommandList() );
            pSerialList = nullptr;
        };
        // 레벨 폭 계측: 병렬로 기록한 레벨 · 패스 수, 이어 기록한 패스 수, 가장 넓은 레벨. 병렬 레벨의 시간은 `RT.Graph.parallelLevel`(기록 · 합류 · 제출)이다.
        size_t parallelLevelCount = 0;
        size_t parallelPassCount  = 0;
        size_t serialPassCount    = 0;
        size_t maxLevelWidth      = 0;
        for ( const vector<PlanPass>& level : _listPlan[_activePlanIndex]._listLevel )
        {
            collectLevelPasses( context, level );
            const vector<ParallelPassEntry>& listPassEntry = _pParallelScratch->_listPassEntry;
            if ( listPassEntry.empty() )
                continue;
            maxLevelWidth              = MathUtil::max( maxLevelWidth, listPassEntry.size() );
            const bool bRecordSerially = bParallelRecording == false || listPassEntry.size() < kMinParallelWidth;
            if ( bRecordSerially )
            {
                serialPassCount += listPassEntry.size();
                if ( pSerialList == nullptr )
                {
                    pSerialList = listPassEntry[0]._pPassCmdList; // 이어 기록하는 동안 다른 패스의 리스트는 이 프레임에 쓰이지 않는다
                    pSerialList->beginCommandList();
                }
                // 레벨의 배리어를 먼저 한 번, 그 뒤 패스를 레벨 순서대로 — 같은 리스트 안의 기록 순서가 GPU 순서다.
                issueBarriers( pSerialList );
                for ( const ParallelPassEntry& entry : listPassEntry )
                {
                    recordPassBody( *entry._pNode, *entry._pListAccess, pSerialList );
                    applyExitAccesses( context, *entry._pNode, *entry._pListAccess );
                }
                continue;
            }
            submitSerialList();
            ++parallelLevelCount;
            parallelPassCount += listPassEntry.size();
            SW_PROFILE_SCOPE( "RT.Graph.parallelLevel" );
            recordAndSubmitParallelLevel( taskManager, device );
            for ( const ParallelPassEntry& entry : listPassEntry )
            {
                applyExitAccesses( context, *entry._pNode, *entry._pListAccess );
            }
        }
        collectReturnBarriers( context );
        if ( _listLevelBarrier.empty() == false )
        {
            if ( pSerialList == nullptr )
            {
                pSerialList = _pParallelScratch->_pReturnCmdList.get();
                pSerialList->beginCommandList();
            }
            issueBarriers( pSerialList );
        }
        submitSerialList();
        SW_PROFILE_COUNT( "RT.Graph.parallelLevels", parallelLevelCount );
        SW_PROFILE_COUNT( "RT.Graph.parallelPasses", parallelPassCount );
        SW_PROFILE_COUNT( "RT.Graph.serialPasses", serialPassCount );
        SW_PROFILE_COUNT( "RT.Graph.maxLevelWidth", maxLevelWidth );
        return true;
    }

    bool RenderGraph::prepareParallelCommandLists( IRHIDevice& device )
    {
        // 엔트리 목록과 노드별 커맨드 리스트는 프레임을 넘어 다시 쓴다(`ParallelScratch`, 완전한 타입은 이 파일에만 있다).
        // 패스마다 프레임마다 새로 만들면 래퍼 하나에 백엔드 할당까지 프레임당 패스 수의 몇 배가 된다.
        if ( _pParallelScratch == nullptr )
            _pParallelScratch = make_unique<ParallelScratch>();
        if ( _pParallelScratch->_pCmdListDevice != &device )
        {
            releaseDeviceResources(); // 지난 디바이스의 리스트다. 새 디바이스에서 다시 만든다.
            _pParallelScratch->_pCmdListDevice = &device;
        }
        ParallelScratch& scratch = *_pParallelScratch;
        if ( scratch._listNodeCmdList.size() < _listNode.size() )
            scratch._listNodeCmdList.resize( _listNode.size() );

        // 모든 레벨의 커맨드 리스트를 **기록과 제출 전에** 마련한다. 만들 수 없으면 아무것도 내지 않고 이 프레임을 실패로 돌려준다.
        // 주의: 레벨을 돌며 만들다 실패했을 때 직렬 `execute` 로 넘어가면, 앞 레벨이 이미 기록과 제출을 마친 뒤라 그 패스들이 **두 번** 돌고
        // 직렬 경로는 커맨드 리스트 없이 기록한다(호출하는 쪽 `FrameRenderer` 가 병렬로 오기 전에 프레임 리스트를 닫아 제출했다).
        for ( const vector<PlanPass>& level : _listPlan[_activePlanIndex]._listLevel )
        {
            for ( const PlanPass& pass : level )
            {
                const RenderGraphNode& node = _listNode[pass._nodeIndex];
                if ( node._execute.isBound() == false )
                    continue;

                unique_ptr<IRHICommandList>& passCmd = scratch._listNodeCmdList[pass._nodeIndex];
                if ( passCmd == nullptr )
                    passCmd = device.createCommandList();
                if ( passCmd == nullptr )
                {
                    // 디바이스가 죽으면 매 프레임 여기로 온다. 같은 오류를 끝없이 되풀이하지 않는다.
                    // 프로세스에 한 번이다. 그래프가 둘 이상이고 서로 다른 스레드에서 돌 수 있어 `exchange` 로 한 번만 남긴다.
                    {
                        SW_LOG_ERROR_ONCE_ID( "Renderer.GraphCommandListUnavailable", "executeParallel: could not create a command list for pass '%#' - nothing was recorded or submitted this frame",
                                              node._name.c_str() );
                    }
                    return false;
                }
            }
        }
        // 되돌릴 자원이 있을 수 있으면 그 전이를 담을 리스트도 미리 만든다(마지막 레벨이 병렬이면 이어 기록하던 리스트가 없다).
        if ( _listImportedResource.empty() == false && scratch._pReturnCmdList == nullptr )
            scratch._pReturnCmdList = device.createCommandList();
        if ( _listImportedResource.empty() == false && scratch._pReturnCmdList == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "Renderer.GraphReturnListUnavailable", "executeParallel: could not create a command list for the final transitions - nothing was recorded or submitted this frame" );
            return false;
        }
        return true;
    }

    void RenderGraph::collectLevelPasses( RenderGraphExecutionContext& context, const vector<PlanPass>& listLevelPass )
    {
        ParallelScratch&           scratch       = *_pParallelScratch;
        vector<ParallelPassEntry>& listPassEntry = scratch._listPassEntry;
        listPassEntry.clear();
        listPassEntry.reserve( listLevelPass.size() );
        _listLevelBarrier.clear();
        _mapLevelBarrierIndex.clear();

        for ( const PlanPass& pass : listLevelPass )
        {
            RenderGraphNode& node = _listNode[pass._nodeIndex];
            appendPassBarriers( context, node, pass._listAccess );

            if ( node._execute.isBound() == false )
                continue;

            // 리스트는 `prepareParallelCommandLists` 가 모두 마련했다.
            listPassEntry.push_back( ParallelPassEntry{ &node, scratch._listNodeCmdList[pass._nodeIndex].get(), &pass._listAccess } );
        }
    }

    void RenderGraph::recordAndSubmitParallelLevel( TaskManager& taskManager, IRHIDevice& device )
    {
        vector<ParallelPassEntry>& listPassEntry = _pParallelScratch->_listPassEntry;

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
        device.setParallelRecording( true );

        // `runParallel` 로 바꾸지 않는다 — 그 티켓은 Normal 큐로 가는데 렌더 스레드가 곧바로 기다리는 기록은 High 레인이어야 한다(아래). 스테이지는 이미 TaskManager 의 것이다.
        TaskStageHandle stage = taskManager.createStage( "RenderGraphLevel" );
        BLOCK( "Submit pass record tasks" )
        {
            TaskManager::SubmitBatch batch{ taskManager }; // 레벨의 패스를 모두 넣은 뒤 한 번만 깨운다. 패스마다 깨우면 그 시그널이 기록 시간의 대부분이었다.
            for ( ParallelPassEntry& entry : listPassEntry )
            {
                TaskHandle handle = taskManager.emplaceTask( "RenderPassRecord", SW_DELEGATE_METHOD( TaskDelegate, &ParallelPassEntry::record, &entry ) );
                if ( handle.isValid() )
                {
                    // 렌더 스레드는 이 스테이지를 곧바로 기다린다. 게임 스레드의 대량 잡(트랜스폼 플러시, 씬 수집)
                    // 뒤에 줄을 서면 그 줄이 그대로 프레임 지연이다. High 레인은 모든 워커가 자기 덱보다 먼저 본다.
                    handle.setPriority( TaskPriority::High );
                    stage.addTask( handle );
                    batch.submit( handle );
                }
                else
                {
                    // 작업을 넣지 못했다(풀이 바닥났거나 종료 중이다). 여기서(렌더 스레드) 직접 기록한다. 그냥 넘어가면 그 리스트가 기록 없이
                    // 아래에서 제출된다. 레벨의 첫 리스트는 **열린 채로**, 나머지는 지난 프레임의 명령 그대로 제출된다.
                    entry.record();
                }
            }
        }

        taskManager.waitStage( stage );
        device.setParallelRecording( false );

        for ( ParallelPassEntry& entry : listPassEntry )
        {
            device.executeCommandList( std::move( entry._closed ) ); // 기록하지 못한 패스는 빈 값이다
        }
    }

    /**
     * @brief 그래프 구성을 Mermaid 다이어그램 텍스트로 내보냅니다.
     */
    string RenderGraph::describeCompiledOrder() const
    {
        if ( _listCompiledLevel.empty() )
            return "render graph: not compiled (or the last compile failed)\n";

        StringBuilder<constant::buffer::kMax8192> out;
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
        return string( out.c_str() );
    }

    string RenderGraph::exportToMermaid() const
    {
        StringBuilder<constant::buffer::kMax4096> builder;
        builder.append( "graph TD\n" );
        for ( const RenderGraphNode& node : _listNode )
        {
            const utf8* pPassLabel = node._name.c_str();

            for ( const hashed_string& input : node._listInput )
            {
                builder.appendFormat( "    %# --> %#\n", input.c_str(), pPassLabel );
            }
            for ( const hashed_string& output : node._listOutput )
            {
                builder.appendFormat( "    %# --> %#\n", pPassLabel, output.c_str() );
            }
        }
        return string( builder.view() );
    }

    /**
     * @brief 그래프 의존 관계를 Graphviz DOT 다이어그램 서식으로 내보냅니다.
     */
    string RenderGraph::exportToDot() const
    {
        StringBuilder<constant::buffer::kMax4096> builder;
        builder.append( "digraph RenderGraph {\n" );
        for ( const RenderGraphNode& node : _listNode )
        {
            const utf8* pPassName = node._name.c_str();

            for ( const hashed_string& input : node._listInput )
            {
                builder.appendFormat( "    \"%#\" -> \"%#\";\n", input.c_str(), pPassName );
            }
            for ( const hashed_string& output : node._listOutput )
            {
                builder.appendFormat( "    \"%#\" -> \"%#\";\n", pPassName, output.c_str() );
            }
        }
        builder.append( "}\n" );
        return string( builder.view() );
    }

    void RenderGraph::appendPassBarriers( RenderGraphExecutionContext& context, const RenderGraphNode& node, const vector<RenderGraphResourceAccess>& listAccess )
    {
        // 비동기 컴퓨트를 받지 않는 그래프는 그 패스를 그래픽스로 계획한다(`computeQueuePlan`) — 전이도 그 큐의 것이다.
        const RHIQueueKind queue = _bAsyncComputeSupported ? node._queue : RHIQueueKind::Graphics;
        for ( const RenderGraphResourceAccess& access : listAccess )
        {
            requestBarrier( context, access, queue );
        }
    }

    void RenderGraph::requestBarrier( RenderGraphExecutionContext& context, const RenderGraphResourceAccess& access, RHIQueueKind queue )
    {
        // 같은 자원을 여러 패스가 요구하면 **한 번만** 낸다(SceneDepth 처럼 여러 패스가 읽는 자원이 레벨마다 읽기 전이를
        // 여러 번 받지 않게 한다).
        const RenderGraphExecutionContext::TrackedAccess before = context.transitionTo( access._resource, access._access );
        const bool                                       bKnown = before._state != RenderGraphResourceState::Undefined;
        // UAV 쓰기 뒤의 UAV 읽기 · 쓰기는 상태가 그대로라도 앞 쓰기를 기다린다(UAV 배리어). UAV 읽기끼리는 기다릴 것이 없다(이미 그 상태 — 아래 같음 판정).
        const bool bUAVAfter = access._access == RenderGraphAccess::UnorderedAccess || access._access == RenderGraphAccess::UnorderedAccessRead;
        const bool bUAVChain = bKnown && before._access == RenderGraphAccess::UnorderedAccess && bUAVAfter;
        if ( bKnown && before._access == access._access && bUAVChain == false )
            return; // 이미 그 상태다. 낼 배리어가 없다.

        const RenderGraphResourceState after = isRenderGraphWriteAccess( access._access ) ? RenderGraphResourceState::Write : RenderGraphResourceState::Read;
        const auto                     it    = _mapLevelBarrierIndex.find( access._resource );
        if ( it != _mapLevelBarrierIndex.end() )
        {
            // 같은 레벨에서 읽기와 쓰기를 함께 요구하는 일은 compile() 이 갈라 놓아 생기지 않는다.
            // 그래도 들어오면 더 강한 쪽(쓰기)을 남긴다.
            if ( after == RenderGraphResourceState::Write )
            {
                _listLevelBarrier[it->second]._after  = after;
                _listLevelBarrier[it->second]._access = access._access;
            }
            return;
        }

        RenderGraphBarrier barrier{};
        barrier._resource    = access._resource;
        barrier._before      = before._state;
        barrier._after       = after;
        barrier._access      = access._access;
        barrier._viewable    = access._viewable;
        barrier._queue       = queue;
        barrier._bUAVBarrier = bUAVChain ? 1 : 0;
        _mapLevelBarrierIndex.emplace( access._resource, _listLevelBarrier.size() );
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

    void RenderGraph::touchResourceLifetime( hashed_string resource, size_t passIndex, uint32 level, bool bWritten )
    {
        const auto it = _mapResourceLifetimeIndex.find( resource );
        if ( it != _mapResourceLifetimeIndex.end() )
        {
            RenderGraphResourceLifetime& life = _listResourceLifetime[it->second];
            life._lastPassIndex               = passIndex;
            life._lastLevel                   = level;
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
        life._firstLevel     = level;
        life._lastLevel      = level;
        life._bWritten       = bWritten;
        life._bRead          = bWritten == false;
        _mapResourceLifetimeIndex.emplace( resource, _listResourceLifetime.size() );
        _listResourceLifetime.push_back( life );
    }

    const RenderGraphNode* RenderGraph::findNode( hashed_string passName ) const
    {
        const auto it = _mapNameToIndex.find( passName );
        return it != _mapNameToIndex.end() ? &_listNode[it->second] : nullptr;
    }

    void RenderGraph::computeResourceLifetimes()
    {
        _listResourceLifetime.clear();
        _mapResourceLifetimeIndex.clear();

        const uint32 lastLevel = _listCompiledLevel.empty() ? 0 : static_cast<uint32>( _listCompiledLevel.size() - 1 );
        size_t       passIndex = 0;
        for ( uint32 level = 0; level < static_cast<uint32>( _listCompiledLevel.size() ); ++level )
        {
            for ( const hashed_string& passName : _listCompiledLevel[level] )
            {
                const RenderGraphNode& node = _listNode[_mapNameToIndex[passName]];
                for ( const hashed_string& input : node._listInput )
                {
                    touchResourceLifetime( input, passIndex, level, false );
                }
                for ( const hashed_string& output : node._listOutput )
                {
                    touchResourceLifetime( output, passIndex, level, true );
                }
                ++passIndex;
            }
        }
        // 비동기 컴퓨트 패스는 뒤 레벨의 그래픽스 일과 GPU 에서 겹친다 — 그 패스가 쓰는 자원은 그래프 끝까지 산다.
        for ( const RenderGraphNode& node : _listNode )
        {
            if ( node._queue != RHIQueueKind::AsyncCompute || _bAsyncComputeSupported == false )
                continue;
            for ( const RenderGraphResourceAccess& access : node._listAccess )
            {
                const auto it = _mapResourceLifetimeIndex.find( access._resource );
                if ( it == _mapResourceLifetimeIndex.end() )
                    continue;
                _listResourceLifetime[it->second]._firstLevel = 0;
                _listResourceLifetime[it->second]._lastLevel  = lastLevel;
            }
        }

        // 그래프보다 오래 사는 자원(히스토리 · 서브시스템의 버퍼)은 그래프 전체를 산다 — 앞뒤 패스 사이의 빈 구간도 내용을 지녀 다른 자원과 메모리를 나눌 수 없다.
        const size_t lastPassIndex = _listCompiledExecutionOrder.empty() ? 0 : _listCompiledExecutionOrder.size() - 1;
        for ( RenderGraphResourceLifetime& life : _listResourceLifetime )
        {
            life._kind = getResourceKind( life._name );
            if ( life._kind == RenderGraphResourceKind::Transient )
                continue;
            life._firstPassIndex = 0;
            life._lastPassIndex  = lastPassIndex;
            life._firstLevel     = 0;
            life._lastLevel      = lastLevel;
        }

        // 아무도 쓰지 않은 것을 읽는 패스는 **지난 프레임 내용이나 0** 을 읽는다. 이미지는 그럴듯하게 나오고
        // 로그는 조용하다. 파이프라인 XML 과 코드가 어긋났을 때 실제로 이렇게 조용히 틀렸다.
        // 그래프 밖에서 채우는 자원(External)과 지난 프레임 값을 읽는 자원(Persistent)은 선언이 그 읽기를 말한다 — 경고하지 않는다.
        for ( const RenderGraphResourceLifetime& life : _listResourceLifetime )
        {
            if ( life._kind == RenderGraphResourceKind::Transient && life._bRead && life._bWritten == false )
            {
                SW_LOG_WARNING_ID( "RenderGraph.ResourceReadButNever", "Resource '%#' is read but never written by any pass — reads stale or zeroed content.",
                                   life._name.c_str() );
            }
        }
    }

    RenderGraphResourceKind RenderGraph::getResourceKind( hashed_string name ) const
    {
        const auto it = _mapResourceKind.find( name );
        return ( it != _mapResourceKind.end() ) ? it->second : RenderGraphResourceKind::Transient;
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
        _listImportedResource.clear();
        releaseDeviceResources();
        _listNode.clear();
        _listCompiledExecutionOrder.clear();
        _listCompiledLevel.clear();
        _mapNameToIndex.clear();
        _listResourceLifetime.clear();
        _mapResourceLifetimeIndex.clear();
        _mapResourceKind.clear();
        _listQueueStep.clear();
        _listFinalJoinBatch.clear();
        _asyncBatchCount = 0;
        _listPlan.clear();
        _activePlanIndex = 0;
    }
} // namespace sw
