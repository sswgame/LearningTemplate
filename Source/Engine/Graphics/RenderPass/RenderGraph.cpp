#include "pch.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RenderPass/RenderGraph.h"
#include "Engine/Utility/Task/TaskManager.h"

namespace sw
{
	/**
	 * @brief 모든 노드 및 실행 순서 초기화
	 */
	void RenderGraph::addPass( hashed_string passName, vector<hashed_string> listInputs, vector<hashed_string> listOutputs,
							   RenderGraphPassExecuteFn execute )
	{
		RenderGraphNode node;
		node._name		  = passName;
		node._listInputs  = std::move( listInputs );
		node._listOutputs = std::move( listOutputs );
		node._execute	  = std::move( execute );
		node._bCulled	  = false;
		_listNodes.push_back( std::move( node ) );
	}

	/**
	 * @brief 그래프에 새 패스 노드 등록
	 */
	bool RenderGraph::compile()
	{
		_listCompiledExecutionOrder.clear();

		if ( _listNodes.empty() )
			return false;

		const size_t nodeCount = _listNodes.size();

		// Active (non-culled) node indices
		vector<size_t> listActiveIndices;
		listActiveIndices.reserve( nodeCount );
		for ( size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
		{
			if ( _listNodes[nodeIndex]._bCulled == false )
				listActiveIndices.push_back( nodeIndex );
		}

		if ( listActiveIndices.empty() )
			return false;

		// 1. Collect all writer passes for each resource in active registration order
		unordered_map<hashed_string, vector<size_t>> mapResourceWriters;
		for ( size_t nodeIndex : listActiveIndices )
		{
			for ( const hashed_string& output : _listNodes[nodeIndex]._listOutputs )
			{
				mapResourceWriters[output].push_back( nodeIndex );
			}
		}

		// Adjacency: producer → consumers; in-degree over active nodes
		unordered_map<size_t, vector<size_t>> adjacency;
		unordered_map<size_t, uint32>		  inDegree;
		adjacency.reserve( listActiveIndices.size() );
		inDegree.reserve( listActiveIndices.size() );

		for ( size_t nodeIndex : listActiveIndices )
		{
			inDegree[nodeIndex] = 0;
		}

		auto addEdge = [&]( size_t from, size_t to )
		{
			if ( from == to )
				return;
			auto& adjList = adjacency[from];
			if ( std::find( adjList.begin(), adjList.end(), to ) == adjList.end() )
			{
				adjList.push_back( to );
				++inDegree[to];
			}
		};

		// 2. Chain consecutive writers of the same resource (Write-after-Write order)
		for ( const auto& [resource, listWriters] : mapResourceWriters )
		{
			for ( size_t writerIndex = 0; writerIndex + 1 < listWriters.size(); ++writerIndex )
			{
				addEdge( listWriters[writerIndex], listWriters[writerIndex + 1] );
			}
		}

		// 3. For each consumer reading an input, find the matching producer
		for ( size_t consumerIndex : listActiveIndices )
		{
			for ( const hashed_string& input : _listNodes[consumerIndex]._listInputs )
			{
				auto it = mapResourceWriters.find( input );
				if ( it == mapResourceWriters.end() || it->second.empty() )
					continue;

				const auto& listWriters	  = it->second;
				size_t		producerIndex = listWriters.front();

				for ( size_t writerIndex : listWriters )
				{
					if ( writerIndex < consumerIndex )
						producerIndex = writerIndex;
				}

				addEdge( producerIndex, consumerIndex );
			}
		}

		std::queue<size_t> queueReady;
		for ( size_t nodeIndex : listActiveIndices )
		{
			if ( inDegree[nodeIndex] == 0 )
				queueReady.push( nodeIndex );
		}

		_listCompiledExecutionOrder.reserve( listActiveIndices.size() );
		while ( queueReady.empty() == false )
		{
			const size_t nodeIndex = queueReady.front();
			queueReady.pop();
			_listCompiledExecutionOrder.push_back( _listNodes[nodeIndex]._name );

			unordered_map<size_t, vector<size_t>>::iterator adjIt = adjacency.find( nodeIndex );
			if ( adjIt == adjacency.end() )
				continue;

			for ( size_t consumerIndex : adjIt->second )
			{
				uint32& degree = inDegree[consumerIndex];
				if ( degree > 0 )
					--degree;
				if ( degree == 0 )
					queueReady.push( consumerIndex );
			}
		}

		if ( _listCompiledExecutionOrder.size() != listActiveIndices.size() )
		{
			SW_LOG_WARNING( "[RenderGraph] Cycle detected during compile — %#/%# active passes scheduled.",
							static_cast<uint32>( _listCompiledExecutionOrder.size() ),
							static_cast<uint32>( listActiveIndices.size() ) );
			_listCompiledExecutionOrder.clear();
			return false;
		}

		_mapNameToIndex.clear();
		_mapNameToIndex.reserve( _listNodes.size() );
		for ( size_t nodeIndex = 0; nodeIndex < _listNodes.size(); ++nodeIndex )
		{
			_mapNameToIndex[_listNodes[nodeIndex]._name] = nodeIndex;
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
				SW_LOG_ERROR( "[RenderGraph] execute: unknown pass in order: %#", passName.c_str() );
				return false;
			}

			RenderGraphNode& node = _listNodes[indexIt->second];
			if ( node._bCulled )
				continue;

			for ( const hashed_string& input : node._listInputs )
			{
				context.transitionTo( input, RenderGraphResourceState::Read );
			}
			for ( const hashed_string& output : node._listOutputs )
			{
				context.transitionTo( output, RenderGraphResourceState::Write );
			}

			if ( node._execute.isBound() )
			{
				RenderGraphPassContext ctx;
				ctx._passName	  = node._name;
				ctx._pListInputs  = &node._listInputs;
				ctx._pListOutputs = &node._listOutputs;
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
			RenderGraphNode*			_pNode{ nullptr };
			unique_ptr<IRHICommandList> _passCmdList{ nullptr };
		};

		vector<ParallelPassEntry> listPassEntries;
		listPassEntries.reserve( _listCompiledExecutionOrder.size() );

		for ( const hashed_string& passName : _listCompiledExecutionOrder )
		{
			const auto indexIt = _mapNameToIndex.find( passName );
			if ( indexIt == _mapNameToIndex.end() )
			{
				SW_LOG_ERROR( "[RenderGraph] executeParallel: unknown pass in order: %#", passName.c_str() );
				return false;
			}

			RenderGraphNode& node = _listNodes[indexIt->second];
			if ( node._bCulled )
				continue;

			for ( const hashed_string& input : node._listInputs )
			{
				context.transitionTo( input, RenderGraphResourceState::Read );
			}
			for ( const hashed_string& output : node._listOutputs )
			{
				context.transitionTo( output, RenderGraphResourceState::Write );
			}

			if ( node._execute.isBound() )
			{
				unique_ptr<IRHICommandList> passCmd = pDevice->createCommandList( RHICommandListMode::Deferred );
				if ( passCmd == nullptr )
				{
					SW_LOG_WARNING( "[RenderGraph] Deferred command list unsupported by RHI — falling back to serial execute" );
					return execute( context );
				}
				listPassEntries.push_back( ParallelPassEntry{ &node, std::move( passCmd ) } );
			}
		}

		if ( listPassEntries.empty() )
			return true;

		TaskStageHandle stage = pTaskManager->createAnonymousStage( "RenderPassStage" );

		for ( ParallelPassEntry& entry : listPassEntries )
		{
			RenderGraphNode* pNode	  = entry._pNode;
			IRHICommandList* pCmdList = entry._passCmdList.get();

			TaskHandle handle = pTaskManager->emplaceTask(
				"RenderPassRecord",
				SW_DELEGATE_LAMBDA( TaskDelegate, [pNode, pCmdList]()
			{
				if ( pNode != nullptr && pCmdList != nullptr && pNode->_execute.isBound() )
				{
					pCmdList->beginCommandList();
					RenderGraphPassContext ctx;
					ctx._passName	  = pNode->_name;
					ctx._pListInputs  = &pNode->_listInputs;
					ctx._pListOutputs = &pNode->_listOutputs;
					ctx._pCmdList	  = pCmdList;
					pNode->_execute( ctx );
					pCmdList->endCommandList();
				}
			} ) );

			if ( handle.isValid() )
			{
				stage.addTask( handle );
				handle.submit();
			}
		}

		pTaskManager->waitStage( stage );

		for ( ParallelPassEntry& entry : listPassEntries )
		{
			if ( entry._passCmdList != nullptr )
			{
				pDevice->executeCommandList( entry._passCmdList.get() );
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

	void RenderGraph::cullUnreferencedPasses( const vector<hashed_string>& listRootOutputs, vector<hashed_string>* pOutCulledPasses )
	{
		unordered_set<hashed_string> uniqueRequiredResources;
		uniqueRequiredResources.reserve( _listNodes.size() * 2 );
		for ( const hashed_string& rootOut : listRootOutputs )
		{
			uniqueRequiredResources.insert( rootOut );
		}

		for ( auto iter = _listNodes.rbegin(); iter != _listNodes.rend(); ++iter )
		{
			RenderGraphNode& node = *iter;
			bool			 producesRequired{ false };
			for ( const hashed_string& output : node._listOutputs )
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
				for ( const hashed_string& input : node._listInputs )
				{
					uniqueRequiredResources.insert( input );
				}
			}
			else
			{
				node._bCulled = true;
				if ( pOutCulledPasses != nullptr )
					pOutCulledPasses->push_back( node._name );
			}
		}

		compile();
	}

	/**
	 * @brief 역방향 종속성 추적을 통해 최종 Target 리소스 생산에 관여하지 않는 패스를 자동 컬링
	 */
	bool RenderGraph::isPassCulled( hashed_string passName ) const
	{
		for ( const RenderGraphNode& node : _listNodes )
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
		result.reserve( _listNodes.size() * 128 );
		for ( const RenderGraphNode& node : _listNodes )
		{
			string passLabel = node._name.c_str();
			if ( node._bCulled )
				passLabel += " (Culled)";

			for ( const hashed_string& input : node._listInputs )
			{
				result += "    " + string( input.c_str() ) + " --> " + passLabel + "\n";
			}
			for ( const hashed_string& output : node._listOutputs )
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
		result.reserve( _listNodes.size() * 128 );
		for ( const RenderGraphNode& node : _listNodes )
		{
			string passName = node._name.c_str();
			if ( node._bCulled )
				result += "    \"" + passName + "\" [style=dashed, color=gray];\n";

			for ( const hashed_string& input : node._listInputs )
			{
				result += "    \"" + string( input.c_str() ) + "\" -> \"" + passName + "\";\n";
			}
			for ( const hashed_string& output : node._listOutputs )
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
		unordered_map<hashed_string, RenderGraphResourceLifetime> mapLifetimes;

		for ( size_t passIndex = 0; passIndex < _listCompiledExecutionOrder.size(); ++passIndex )
		{
			const hashed_string passName = _listCompiledExecutionOrder[passIndex];
			auto				it		 = _mapNameToIndex.find( passName );
			if ( it == _mapNameToIndex.end() )
				continue;

			const RenderGraphNode& node = _listNodes[it->second];
			for ( const hashed_string& input : node._listInputs )
			{
				auto lifeIt = mapLifetimes.find( input );
				if ( lifeIt == mapLifetimes.end() )
				{
					RenderGraphResourceLifetime life{};
					life._name			 = input;
					life._firstPassIndex = passIndex;
					life._lastPassIndex	 = passIndex;
					life._bRead			 = true;
					mapLifetimes[input]	 = life;
				}
				else
				{
					lifeIt->second._lastPassIndex = passIndex;
					lifeIt->second._bRead		  = true;
				}
			}

			for ( const hashed_string& output : node._listOutputs )
			{
				auto lifeIt = mapLifetimes.find( output );
				if ( lifeIt == mapLifetimes.end() )
				{
					RenderGraphResourceLifetime life{};
					life._name			 = output;
					life._firstPassIndex = passIndex;
					life._lastPassIndex	 = passIndex;
					life._bWritten		 = true;
					mapLifetimes[output] = life;
				}
				else
				{
					lifeIt->second._lastPassIndex = passIndex;
					lifeIt->second._bWritten	  = true;
				}
			}
		}

		vector<RenderGraphResourceLifetime> listResult;
		listResult.reserve( mapLifetimes.size() );
		for ( auto& [name, life] : mapLifetimes )
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
		_listNodes.clear();
		_listCompiledExecutionOrder.clear();
		_mapNameToIndex.clear();
	}
} // namespace sw
