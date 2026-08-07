#include "pch.h"
#include "RenderGraph.h"

/**
 * @file RenderGraph.cpp
 * @brief Render Graph 알고리즘 구현 (컬링, 위상 정렬 및 Mermaid/Dot 그래프 내보내기)
 */

namespace sw
{
	/**
	 * @brief 그래프에 새 패스 노드 등록
	 */
	void RenderGraph::addPass( hashed_string passName, std::vector<hashed_string> inputs, std::vector<hashed_string> outputs )
	{
		RenderGraphNode node;
		node._name	  = passName;
		node._inputs  = std::move( inputs );
		node._outputs = std::move( outputs );
		node._bCulled = false;
		_nodes.push_back( std::move( node ) );
	}

	/**
	 * @brief 그래프 컴파일: 활성화된 패스 순서대로 실행 시퀀스 구축
	 */
	bool RenderGraph::compile()
	{
		_compiledExecutionOrder.clear();

		for ( const RenderGraphNode& node : _nodes )
		{
			if ( node._bCulled == false )
			{
				_compiledExecutionOrder.push_back( node._name );
			}
		}

		return _nodes.empty() == false;
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
	}
}
