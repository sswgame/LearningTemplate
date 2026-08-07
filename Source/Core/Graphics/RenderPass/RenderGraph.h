#pragma once

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Graphics/RenderPass/RenderPassResource.h"

/**
 * @file RenderGraph.h
 * @brief 프레임버퍼 렌더 패스 간 데이터 의존성을 분석하고 DAG 기반 스케줄링을 수행하는 RenderGraph 클래스 정의
 */

namespace sw
{
	/**
	 * @struct RenderGraphNode
	 * @brief Render Graph 내 단일 렌더 패스 노드 서술체
	 */
	struct RenderGraphNode
	{
		hashed_string			   _name;             ///< 렌더 패스 고유 이름 (해시)
		std::vector<hashed_string> _inputs;	          ///< 입력 종속 자원 해시 리스트
		std::vector<hashed_string> _outputs;          ///< 출력 생산 자원 해시 리스트
		bool					   _bCulled = false;  ///< 미사용 패스 컬링 여부
	};

	/**
	 * @class RenderGraph
	 * @brief 렌더 패스 간 자원 의존 관계를 빌드하고 위상 정렬(Topological Sort)을 통해 최적 실행 순서를 산출하는 프레임워크
	 */
	class SW_API RenderGraph
	{
	public:
		RenderGraph() = default;

		RenderGraph( const RenderGraph& )			 = delete;
		RenderGraph& operator=( const RenderGraph& ) = delete;
		RenderGraph( RenderGraph&& )				 = default;
		RenderGraph& operator=( RenderGraph&& )		 = default;

		/**
		 * @brief 렌더 그래프에 새 렌더 패스 노드 추가
		 * @param passName 렌더 패스 이름 (hashed_string)
		 * @param inputs 패스가 읽을 입력 자원 리스트
		 * @param outputs 패스가 기록할 출력 자원 리스트
		 */
		void addPass( hashed_string passName, std::vector<hashed_string> inputs = {}, std::vector<hashed_string> outputs = {} );

		/**
		 * @brief 렌더 패스 간 의존성 검사 및 위상 정렬 수행
		 * @return 성공 시 true
		 */
		bool compile();

		/**
		 * @brief 최종 목표 출력 타깃에 도달하지 않는 불필요한 미사용 패스 컬링(Culling)
		 * @param targetResourceName 최종 타깃 리소스 해시 이름
		 */
		void cullUnusedPasses( hashed_string targetResourceName );

		/** @brief 컴파일 완료된 위상 정렬 패스 실행 순서 반환 */
		const std::vector<hashed_string>& getExecutionOrder() const { return _compiledExecutionOrder; }

		/** @brief 그래프 내 총 패스 노드 개수 반환 */
		uint32 getNodeCount() const { return static_cast<uint32>( _nodes.size() ); }

		/** @brief 특정 렌더 패스가 컬링되었는지 여부 확인 */
		bool isPassCulled( hashed_string passName ) const;

		/** @brief 디버깅 및 가시화를 위해 현 그래프 구성을 Mermaid 다이어그램 텍스트로 내보내기 */
		std::string exportToMermaid() const;

		/** @brief 디버깅을 위해 현 그래프 구성을 Graphviz DOT 다이어그램 텍스트로 내보내기 */
		std::string exportToDot() const;

		/** @brief 모든 패스 및 컴파일 상태 초기화 */
		void clear();

	private:
		std::vector<RenderGraphNode> _nodes;
		std::vector<hashed_string>  _compiledExecutionOrder;
	};
}
