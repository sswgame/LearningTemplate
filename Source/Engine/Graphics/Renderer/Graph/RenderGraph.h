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
    class IRHICommandList;
    class IRHIDevice;
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
     * @struct RenderGraphBarrier
     * @brief 그래프가 **추론한** 상태 전이 하나 — "이 자원을 이 상태로 바꿔야 한다".
     * @details 예전엔 웨이브가 읽고 쓰는 자원 **이름을 전부** 넘겼고, 받는 쪽이 그걸 그대로 전이 호출로
     *          옮겼다. 같은 자원을 다섯 패스가 읽으면 다섯 번 불렀고, 이미 그 상태인 것도 다시 불렀다.
     *          이제 그래프가 자기가 들고 있는 상태와 비교해서 **실제로 바뀌는 것만** 낸다.
     *
     *          `_before` 는 진단용이다 — 어디서 무엇이 어떻게 바뀌었는지 로그·테스트가 보려면 필요하다.
     */
    struct RenderGraphBarrier
    {
        hashed_string            _resource;
        RenderGraphResourceState _before{ RenderGraphResourceState::Undefined };
        RenderGraphResourceState _after{ RenderGraphResourceState::Undefined };
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
     * @struct RenderGraphWaveContext
     * @brief 한 웨이브를 **병렬로 기록하기 직전에**, 단일 스레드에서 한 번 전달되는 컨텍스트
     * @details 이 웨이브를 위해 **실제로 필요한 전이만** 들어 있다. 받는 쪽은 이름을 텍스처로 풀어
     *          `prepareTextureForShaderRead` / `prepareTextureForRenderTarget` 을 미리 불러 둔다 —
     *          그러면 패스 콜백은 이미 맞는 상태를 보게 되어 기록 중에 리소스 상태를 바꾸지 않는다.
     */
    struct RenderGraphWaveContext
    {
        /// @brief 이 웨이브 전에 걸어야 할 전이. 중복도, 이미 맞는 상태도 없다 (그래프가 걸러 낸다).
        const vector<RenderGraphBarrier>* _pListBarrier{ nullptr };
        /// @brief 배리어를 기록할 커맨드 리스트. 병렬 경로는 프레임 스트림(웨이브보다 먼저 실행된다).
        IRHICommandList* _pCmdList{ nullptr };
    };

    using RenderGraphWavePrologueFn = Delegate<void( const RenderGraphWaveContext& )>;

    /**
     * @struct RenderGraphExecutionContext
     * @brief execute() 호출 시 임시 상태 및 메모리를 관리하는 컨텍스트
     */
    struct RenderGraphExecutionContext
    {
        vector<pair<hashed_string, RenderGraphResourceState>> _listResourceState;
        unordered_map<hashed_string, size_t>                  _mapResourceToIndex;
        uint32                                                _lastTransitionCount{ 0 };

        /**
         * @brief 다음 execute() 호출 전 상태 초기화 (capacity 재사용으로 Zero-allocation 유지).
         * @details **프레임마다 잊는 것은 일부러다.** 전이는 그래프 밖에서도 일어난다 — 패스가 선언하지
         *          않은 텍스처를 registerPassTexture 로 걸거나, 리드백·에디터 블릿이 상태를 바꾼다.
         *          지난 프레임의 믿음을 이어 가면 그런 자리에서 **필요한 배리어를 건너뛴다**. 그래서
         *          프레임 시작에는 아무것도 모르는 것으로 치고, 첫 사용마다 전이를 낸다. 진짜 상태를 아는
         *          것은 백엔드이고(DX12 는 상태가 같으면 배리어를 안 쏜다) 여기서 버는 것은 **한 프레임
         *          안의 중복**이다 — 같은 자원을 세 패스가 읽어도 전이는 한 번이다.
         */
        void reset()
        {
            _listResourceState.clear();
            _mapResourceToIndex.clear();
            _lastTransitionCount = 0;
        }

        /**
         * @brief 리소스 상태를 전이하고 **이전 상태**를 돌려줍니다 (O(1) 해시 맵).
         * @details 반환값이 desired 와 같으면 이미 그 상태라 배리어를 낼 것이 없다. 처음 보는 자원은
         *          Undefined 였던 것으로 친다 — 그 첫 전이는 실제로 필요하다.
         */
        RenderGraphResourceState transitionTo( hashed_string resource, RenderGraphResourceState desired )
        {
            auto it = _mapResourceToIndex.find( resource );
            if ( it != _mapResourceToIndex.end() )
            {
                auto&                          entry  = _listResourceState[it->second];
                const RenderGraphResourceState before = entry.second;
                if ( before != desired )
                {
                    entry.second = desired;
                    ++_lastTransitionCount;
                }
                return before;
            }

            const size_t index = _listResourceState.size();
            _listResourceState.push_back( { resource, desired } );
            _mapResourceToIndex[resource] = index;
            ++_lastTransitionCount;
            return RenderGraphResourceState::Undefined;
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
         * @brief 웨이브를 병렬 기록하기 직전에 부를 콜백을 등록합니다 (배리어 선행 발행용).
         * @details 등록하지 않으면 아무 일도 하지 않는다 — 직렬 실행 경로에는 필요 없다.
         */
        void setWavePrologue( const RenderGraphWavePrologueFn& prologue ) { _wavePrologue = prologue; }

        /**
         * @brief 렌더 패스 간 의존성 검사 및 위상 정렬 수행
         * @return 성공 시 true
         */
        bool compile();

        /**
         * @brief 컴파일된 위상 순서로 패스 콜백을 실행합니다.
         * @details 패스마다 필요한 상태 전이를 **추론해서** 웨이브 콜백에 넘긴 뒤 패스를 부릅니다.
         *          실행 순서가 비어 있으면 compile()을 한 번 시도합니다.
         * @param context 실행 중 자원 상태 관리를 위한 임시 메모리 컨텍스트
         * @param pCmdList (선택) 배리어를 기록할 커맨드 리스트. 직렬 경로는 패스가 기록하는 리스트와
         *                 **같은 것**을 넘겨야 한다 — 배리어가 그 패스 바로 앞에 들어가야 하기 때문이다.
         *                 nullptr 이면 상태만 추적하고 배리어는 내지 않는다(GPU 없는 테스트).
         * @return 실행 성공 시 true (사이클/컴파일 실패 시 false)
         */
        bool execute( RenderGraphExecutionContext& context, IRHICommandList* pCmdList = nullptr );

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

        /**
         * @brief 컴파일된 실행 순서를 의존성 웨이브(레벨) 단위로 묶어 반환합니다.
         * @details 같은 웨이브 안의 패스들은 서로 입출력 의존이 없어(Kahn 위상 정렬의 같은 BFS 레벨)
         *          안전하게 동시에(병렬로) 기록할 수 있습니다. 웨이브 사이에는 순서를 지켜야 합니다.
         *          executeParallel()이 이 구조를 써서 웨이브 안에서만 병렬 기록하고 웨이브 경계에서
         *          동기화합니다.
         */
        const vector<vector<hashed_string>>& getExecutionWaves() const { return _listCompiledWave; }

        /** @brief 그래프 내 총 패스 노드 개수 반환 */
        uint32 getNodeCount() const { return static_cast<uint32>( _listNode.size() ); }

        /** @brief 특정 렌더 패스가 컬링되었는지 여부 확인 */
        bool isPassCulled( hashed_string passName ) const;

        /**
         * @brief 각 리소스의 수명(첫 ~ 마지막 사용 패스 인덱스). compile() 이 계산해 둔 것을 돌려줍니다.
         * @details 예전엔 부를 때마다 다시 계산했고 아무도 부르지 않았다. 지금은 compile() 이 채우고
         *          `validateResourceUsage` 가 그 자리에서 읽는다 — 수명이 그래프의 산출물이 됐다.
         */
        const vector<RenderGraphResourceLifetime>& getResourceLifetimes() const { return _listResourceLifetime; }
        /** @brief 리소스 하나의 수명. 그래프가 모르는 이름이면 nullptr. */
        const RenderGraphResourceLifetime* findResourceLifetime( hashed_string name ) const;

        /** @brief 디버깅 및 가시화를 위해 현 그래프 구성을 Mermaid 다이어그램 텍스트로 내보내기 */
        string exportToMermaid() const;

        /** @brief 디버깅을 위해 현 그래프 구성을 Graphviz DOT 다이어그램 텍스트로 내보내기 */
        string exportToDot() const;

        /** @brief 모든 패스 및 컴파일 상태 초기화 */
        void clear();

    private:
        vector<RenderGraphNode> _listNode;
        vector<hashed_string>   _listCompiledExecutionOrder;
        /** @brief _listCompiledExecutionOrder와 같은 순서·내용을 웨이브(의존성 레벨) 단위로 묶은 것. */
        vector<vector<hashed_string>> _listCompiledWave;

        // 핫패스 무할당용 캐시
        unordered_map<hashed_string, size_t> _mapNameToIndex;

        /// @brief 웨이브를 병렬 기록하기 직전에 부르는 콜백 — setWavePrologue 참고.
        RenderGraphWavePrologueFn _wavePrologue;
        /// @brief 이번 웨이브(직렬이면 이번 패스)의 배리어 — 프레임마다 재사용해 할당을 반복하지 않는다.
        vector<RenderGraphBarrier> _listWaveBarrier;
        /// @brief 배리어를 추리는 동안 쓰는 스크래치: 자원 이름 → `_listWaveBarrier` 인덱스 (웨이브 안 중복 제거).
        unordered_map<hashed_string, size_t> _mapWaveBarrierIndex;
        /// @brief compile() 이 계산한 리소스 수명 — 첫/마지막 사용 패스와 읽힘/쓰임 여부.
        vector<RenderGraphResourceLifetime>  _listResourceLifetime;
        unordered_map<hashed_string, size_t> _mapResourceLifetimeIndex;

        /**
         * @brief 패스 하나가 필요로 하는 전이를 `_listWaveBarrier` 에 보탭니다.
         * @details 같은 웨이브 안에서 한 자원을 읽기와 쓰기로 동시에 요구할 일은 없다 — compile() 이
         *          RAW/WAW 엣지로 그런 패스들을 다른 웨이브에 갈라 놓기 때문이다. 그래도 들어오면 쓰기를
         *          택한다(더 강한 상태다). 이미 그 상태인 자원은 아예 넣지 않는다.
         */
        void appendPassBarriers( RenderGraphExecutionContext& context, const RenderGraphNode& node );
        /** @brief 추린 배리어를 웨이브 콜백에 넘깁니다. 콜백이 없거나 낼 것이 없으면 아무 일도 하지 않습니다. */
        void issueBarriers( IRHICommandList* pCmdList );
        /** @brief compile() 끝에서 리소스 수명을 채우고, 쓰이지 않는/쓰인 적 없는 자원을 로그로 알립니다. */
        void buildResourceLifetimes();
    };
} // namespace sw
