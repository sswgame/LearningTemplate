/**
 * @file RenderGraph.h
 * @brief 프레임버퍼 렌더 패스 간 데이터 의존성을 분석하고 DAG 기반 스케줄링을 수행하는 RenderGraph 클래스 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class IRHIDevice;
    class IRHICommandList;
    class TaskManager;

    /**
     * @enum RenderGraphResourceState
     * @brief 논리 리소스 접근 상태 (RHI barrier API 부재 시 전이 추적용)
     */
    enum class RenderGraphResourceState : uint8
    {
        Undefined, ///< 미사용 / 미정의
        Read,      ///< 패스 입력(읽기)
        Write,     ///< 패스 출력(쓰기)
    };

    /**
     * @struct RenderGraphPassContext
     * @brief execute() 중 패스 콜백에 전달되는 컨텍스트
     */
    struct RenderGraphPassContext
    {
        hashed_string                _passName; ///< 실행 중인 패스 이름
        const vector<hashed_string>* _pListInputs{ nullptr };
        const vector<hashed_string>* _pListOutputs{ nullptr };
        IRHICommandList*             _pCmdList{ nullptr }; ///< (선택) 병렬 레코딩 시 활성 패스 커맨드 리스트
    };

    using RenderGraphPassExecuteFn = Delegate<void( const RenderGraphPassContext& )>;

    /**
     * @struct RenderGraphExecutionContext
     * @brief execute() 호출 시 임시 상태 및 메모리를 관리하는 컨텍스트
     */
    struct RenderGraphExecutionContext
    {
        vector<pair<hashed_string, RenderGraphResourceState>> _listResourceState;
        unordered_map<hashed_string, size_t>                  _mapResourceToIndex;
        uint32                                                _lastTransitionCount{ 0 };

        /** @brief 다음 execute() 호출 전 상태 초기화 (capacity 재사용으로 Zero-allocation 유지) */
        void reset()
        {
            _listResourceState.clear();
            _mapResourceToIndex.clear();
            _lastTransitionCount = 0;
        }

        /** @brief O(1) 해시 맵 인덱싱 기반으로 리소스 상태를 전이하고 카운터를 갱신합니다. */
        void transitionTo( hashed_string resource, RenderGraphResourceState desired )
        {
            auto it = _mapResourceToIndex.find( resource );
            if ( it != _mapResourceToIndex.end() )
            {
                auto& entry = _listResourceState[it->second];
                if ( entry.second != desired )
                {
                    entry.second = desired;
                    ++_lastTransitionCount;
                }
            }
            else
            {
                const size_t index = _listResourceState.size();
                _listResourceState.push_back( { resource, desired } );
                _mapResourceToIndex[resource] = index;
                ++_lastTransitionCount;
            }
        }
    };

    /**
     * @struct RenderGraphNode
     * @brief Render Graph 내 단일 렌더 패스 노드 서술체
     */
    struct RenderGraphNode
    {
        hashed_string            _name;             ///< 렌더 패스 고유 이름 (해시)
        vector<hashed_string>    _listInput;        ///< 입력 종속 자원 해시 리스트
        vector<hashed_string>    _listOutput;       ///< 출력 생산 자원 해시 리스트
        RenderGraphPassExecuteFn _execute;          ///< 컴파일된 순서대로 호출되는 패스 콜백 (선택)
        bool                     _bCulled{ false }; ///< 미사용 패스 컬링 여부
    };

    /**
     * @struct RenderGraphResourceLifetime
     * @brief Transient Resource Aliasing 계산을 위한 리소스 사용 주기
     */
    struct RenderGraphResourceLifetime
    {
        hashed_string _name;
        size_t        _firstPassIndex{ 0 };
        size_t        _lastPassIndex{ 0 };
        bool          _bWritten{ false };
        bool          _bRead{ false };
    };

    /**
     * @class RenderGraph
     * @brief 렌더 패스 간 자원 의존 관계를 빌드하고 위상 정렬(Topological Sort)을 통해 최적 실행 순서를 산출하는 프레임워크
     */
    class SW_API RenderGraph
    {
    public:
        /** @brief 빈 그래프입니다. */
        RenderGraph() = default;

        /** @brief 복사를 금지합니다. */
        RenderGraph( const RenderGraph& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderGraph& operator=( const RenderGraph& ) = delete;
        /** @brief 이동 생성자입니다. */
        RenderGraph( RenderGraph&& ) = default;
        /** @brief 이동 대입입니다. */
        RenderGraph& operator=( RenderGraph&& ) = default;

        /**
         * @brief 렌더 그래프에 새 렌더 패스 노드 추가
         * @param passName 렌더 패스 이름 (hashed_string)
         * @param listInput 패스가 읽을 입력 자원 리스트
         * @param listOutput 패스가 기록할 출력 자원 리스트
         * @param execute 위상 정렬 실행 시 호출할 콜백 (미바인딩이면 스킵)
         */
        void addPass( hashed_string passName, vector<hashed_string> listInput = {}, vector<hashed_string> listOutput = {},
                      RenderGraphPassExecuteFn execute = {} );

        /**
         * @brief 렌더 패스 간 의존성 검사 및 위상 정렬 수행
         * @return 성공 시 true
         */
        bool compile();

        /**
         * @brief 컴파일된 위상 순서로 패스 콜백을 실행합니다.
         * @details 입력/출력에 대해 논리 리소스 상태 전이를 기록합니다(RHI barrier 미연동).
         *          실행 순서가 비어 있으면 compile()을 한 번 시도합니다.
         * @param context 실행 중 자원 상태 관리를 위한 임시 메모리 컨텍스트
         * @return 실행 성공 시 true (사이클/컴파일 실패 시 false)
         */
        bool execute( RenderGraphExecutionContext& context );

        /**
         * @brief TaskManager 워커 스레드들을 활용하여 렌더 패스 명령을 병렬로 기록하고 제출합니다.
         * @param context 실행 중 자원 상태 관리를 위한 임시 메모리 컨텍스트
         * @param pTaskManager 백그라운드 태스크 매니저 (nullptr일 경우 직렬 execute()로 폴백)
         * @param pDevice RHI 디바이스 (커맨드 리스트 생성 및 제출용)
         * @return 실행 성공 시 true
         */
        bool executeParallel( RenderGraphExecutionContext& context, TaskManager* pTaskManager, IRHIDevice* pDevice );

        /**
         * @brief 최종 목표 출력 타깃에 도달하지 않는 불필요한 미사용 패스 컬링(Culling)
         * @param targetResourceName 최종 타깃 리소스 해시 이름
         */
        void cullUnusedPasses( hashed_string targetResourceName );

        /**
         * @brief 여러 최종 목표 출력 타깃들에 도달하지 않는 불필요한 미사용 패스 일괄 컬링(Culling)
         * @param listRootOutput 최종 루트 타깃 리소스 해시 리스트
         * @param pOutListCulledPass (선택) 컬링된 패스 이름들을 반환받을 출력 파라미터
         */
        void cullUnreferencedPasses( const vector<hashed_string>& listRootOutput, vector<hashed_string>* pOutListCulledPass = nullptr );

        /** @brief 컴파일 완료된 위상 정렬 패스 실행 순서 반환 */
        const vector<hashed_string>& getExecutionOrder() const { return _listCompiledExecutionOrder; }

        /** @brief 그래프 내 총 패스 노드 개수 반환 */
        uint32 getNodeCount() const { return static_cast<uint32>( _listNode.size() ); }

        /** @brief 특정 렌더 패스가 컬링되었는지 여부 확인 */
        bool isPassCulled( hashed_string passName ) const;

        /** @brief Transient Resource Aliasing을 위한 각 리소스별 수명 주기(First ~ Last Pass Index) 계산 */
        vector<RenderGraphResourceLifetime> computeResourceLifetimes() const;

        /** @brief 디버깅 및 가시화를 위해 현 그래프 구성을 Mermaid 다이어그램 텍스트로 내보내기 */
        string exportToMermaid() const;

        /** @brief 디버깅을 위해 현 그래프 구성을 Graphviz DOT 다이어그램 텍스트로 내보내기 */
        string exportToDot() const;

        /** @brief 모든 패스 및 컴파일 상태 초기화 */
        void clear();

    private:
        vector<RenderGraphNode> _listNode;
        vector<hashed_string>   _listCompiledExecutionOrder;

        // 핫패스 무할당용 캐시
        unordered_map<hashed_string, size_t> _mapNameToIndex;
    };
} // namespace sw
