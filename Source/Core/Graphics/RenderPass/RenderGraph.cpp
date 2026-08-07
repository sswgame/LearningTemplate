#include "pch.h"
#include "RenderGraph.h"
#include "Core/Utility/Log/Logger.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>

/**
 * @file RenderGraph.cpp
 * @brief Render Graph 알고리즘 구현 (컬링, 위상 정렬 및 Mermaid/Dot 그래프 내보내기)
 */

namespace sw
{
	/**
	 * @brief 그래프에 새 패스 노드 등록
	 */
	void RenderGraph::addPass( hashed_string passName, std::vector<hashed_string> inputs, std::vector<hashed_string> outputs,
							   RenderGraphPassExecuteFn execute )
	{
		RenderGraphNode node;
		node._name	  = passName;
		node._inputs  = std::move( inputs );
		node._outputs = std::move( outputs );
		node._execute = std::move( execute );
		node._bCulled = false;
		_nodes.push_back( std::move( node ) );
	}

	/**
	 * @brief 그래프 컴파일: 리소스 의존성 기준 Kahn 위상 정렬로 실행 시퀀스 구축
	 */
	bool RenderGraph::compile()
	{
		_compiledExecutionOrder.clear();

		if ( _nodes.empty() )
			return false;

		const size_t nodeCount = _nodes.size();

		// Active (non-culled) node indices
		std::vector<size_t> activeIndices;
		activeIndices.reserve( nodeCount );
		for ( size_t i = 0; i < nodeCount; ++i )
		{
			if ( _nodes[i]._bCulled == false )
				activeIndices.push_back( i );
		}

		if ( activeIndices.empty() )
			return true;

		// resource → producer pass index (last writer wins for multi-write)
		std::unordered_map<hashed_string, size_t> resourceProducer;
		resourceProducer.reserve( activeIndices.size() * 2 );
		for ( size_t nodeIndex : activeIndices )
		{
			for ( const hashed_string& output : _nodes[nodeIndex]._outputs )
				resourceProducer[output] = nodeIndex;
		}

		// Adjacency: producer → consumers; in-degree over active nodes
		std::unordered_map<size_t, std::vector<size_t>> adjacency;
		std::unordered_map<size_t, uint32>				inDegree;
		adjacency.reserve( activeIndices.size() );
		inDegree.reserve( activeIndices.size() );

		for ( size_t nodeIndex : activeIndices )
			inDegree[nodeIndex] = 0;

		for ( size_t consumerIndex : activeIndices )
		{
			std::unordered_set<size_t> seenProducers;
			for ( const hashed_string& input : _nodes[consumerIndex]._inputs )
			{
				auto producerIt = resourceProducer.find( input );
				if ( producerIt == resourceProducer.end() )
					continue;

				const size_t producerIndex = producerIt->second;
				if ( producerIndex == consumerIndex )
					continue;
				if ( seenProducers.insert( producerIndex ).second == false )
					continue;

				adjacency[producerIndex].push_back( consumerIndex );
				++inDegree[consumerIndex];
			}
		}

		std::queue<size_t> ready;
		for ( size_t nodeIndex : activeIndices )
		{
			if ( inDegree[nodeIndex] == 0 )
				ready.push( nodeIndex );
		}

		_compiledExecutionOrder.reserve( activeIndices.size() );
		while ( ready.empty() == false )
		{
			const size_t nodeIndex = ready.front();
			ready.pop();
			_compiledExecutionOrder.push_back( _nodes[nodeIndex]._name );

			auto adjIt = adjacency.find( nodeIndex );
			if ( adjIt == adjacency.end() )
				continue;

			for ( size_t consumerIndex : adjIt->second )
			{
				uint32& degree = inDegree[consumerIndex];
				if ( degree > 0 )
					--degree;
				if ( degree == 0 )
					ready.push( consumerIndex );
			}
		}

		if ( _compiledExecutionOrder.size() != activeIndices.size() )
		{
			SW_LOG_ERROR( "[RenderGraph] Cycle detected during compile — %u/%u active passes scheduled.",
						  static_cast<uint32>( _compiledExecutionOrder.size() ),
						  static_cast<uint32>( activeIndices.size() ) );
			_compiledExecutionOrder.clear();
			return false;
		}

		return true;
	}

	/**
	 * @brief 컴파일된 위상 순서로 패스 콜백 실행 + 논리 리소스 전이 추적
	 */
	bool RenderGraph::execute()
	{
		_lastTransitionCount = 0;

		if ( _compiledExecutionOrder.empty() )
		{
			if ( compile() == false )
				return false;
		}

		std::unordered_map<hashed_string, size_t> nameToIndex;
		nameToIndex.reserve( _nodes.size() );
		for ( size_t i = 0; i < _nodes.size(); ++i )
			nameToIndex[_nodes[i]._name] = i;

		std::unordered_map<hashed_string, RenderGraphResourceState> resourceStates;
		resourceStates.reserve( _compiledExecutionOrder.size() * 2 );

		auto transitionTo = [this, &resourceStates]( hashed_string resource, RenderGraphResourceState desired )
		{
			auto it = resourceStates.find( resource );
			const RenderGraphResourceState previous =
				( it != resourceStates.end() ) ? it->second : RenderGraphResourceState::Undefined;
			if ( previous != desired )
			{
				++_lastTransitionCount;
				resourceStates[resource] = desired;
			}
			else if ( it == resourceStates.end() )
			{
				resourceStates[resource] = desired;
			}
		};

		for ( const hashed_string& passName : _compiledExecutionOrder )
		{
			auto indexIt = nameToIndex.find( passName );
			if ( indexIt == nameToIndex.end() )
			{
				SW_LOG_ERROR( "[RenderGraph] execute: unknown pass in order: %#", passName.c_str() );
				return false;
			}

			RenderGraphNode& node = _nodes[indexIt->second];
			if ( node._bCulled )
				continue;

			for ( const hashed_string& input : node._inputs )
				transitionTo( input, RenderGraphResourceState::Read );
			for ( const hashed_string& output : node._outputs )
				transitionTo( output, RenderGraphResourceState::Write );

			if ( node._execute.isBound() )
			{
				RenderGraphPassContext ctx;
				ctx._passName = node._name;
				ctx._inputs	  = &node._inputs;
				ctx._outputs  = &node._outputs;
				node._execute( ctx );
			}
		}

		return true;
	}

	/**
	 * @brief 역방향 종속성 추적을 통해 최종 Target 리소스 생산에 관여하지 않는 패스를 자동 컬링
	 */
	void RenderGraph::cullUnusedPasses( hashed_string targetResourceName )
	{
		std::unordered_set<hashed_string> requiredResources;
		requiredResources.reserve( _nodes.size() * 2 );
		requiredResources.insert( targetResourceName );

		for ( auto iter = _nodes.rbegin(); iter != _nodes.rend(); ++iter )
		{
			RenderGraphNode& node			  = *iter;
			bool			 producesRequired = false;
			for ( const hashed_string& output : node._outputs )
			{
				if ( requiredResources.find( output ) != requiredResources.end() )
				{
					producesRequired = true;
					break;
				}
			}

			if ( producesRequired )
			{
				node._bCulled = false;
				for ( const hashed_string& input : node._inputs )
				{
					requiredResources.insert( input );
				}
			}
			else
			{
				node._bCulled = true;
			}
		}

		compile();
	}

	/**
	 * @brief 지정된 패스가 컬링되었는지 여부 확인
	 */
	bool RenderGraph::isPassCulled( hashed_string passName ) const
	{
		for ( const RenderGraphNode& node : _nodes )
		{
			if ( node._name == passName )
			{
				return node._bCulled;
			}
		}
		return false;
	}

	/**
	 * @brief Render Graph 의존 관계를 Mermaid 다이어그램 서식으로 출력
	 */
	std::string RenderGraph::exportToMermaid() const
	{
		std::string result = "graph TD\n";
		result.reserve( _nodes.size() * 128 );
		for ( const RenderGraphNode& node : _nodes )
		{
			std::string passLabel = node._name.c_str();
			if ( node._bCulled )
			{
				passLabel += " (Culled)";
			}

			for ( const hashed_string& input : node._inputs )
			{
				result += "    " + std::string( input.c_str() ) + " --> " + passLabel + "\n";
			}
			for ( const hashed_string& output : node._outputs )
			{
				result += "    " + passLabel + " --> " + std::string( output.c_str() ) + "\n";
			}
		}
		return result;
	}

	/**
	 * @brief Render Graph 의존 관계를 Graphviz DOT 다이어그램 서식으로 출력
	 */
	std::string RenderGraph::exportToDot() const
	{
		std::string result = "digraph RenderGraph {\n";
		result.reserve( _nodes.size() * 128 );
		for ( const RenderGraphNode& node : _nodes )
		{
			std::string passName = node._name.c_str();
			if ( node._bCulled )
			{
				result += "    \"" + passName + "\" [style=dashed, color=gray];\n";
			}

			for ( const hashed_string& input : node._inputs )
			{
				result += "    \"" + std::string( input.c_str() ) + "\" -> \"" + passName + "\";\n";
			}
			for ( const hashed_string& output : node._outputs )
			{
				result += "    \"" + passName + "\" -> \"" + std::string( output.c_str() ) + "\";\n";
			}
		}
		result += "}\n";
		return result;
	}

	/**
	 * @brief 모든 노드 및 실행 순서 초기화
	 */
	void RenderGraph::clear()
	{
		_nodes.clear();
		_compiledExecutionOrder.clear();
		_lastTransitionCount = 0;
	}
} // namespace sw
