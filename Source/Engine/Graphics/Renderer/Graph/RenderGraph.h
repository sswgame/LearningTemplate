/**
 * @file RenderGraph.h
 * @brief 렌더 패스 사이의 데이터 의존성을 분석해 DAG 기반으로 스케줄링하는 RenderGraph 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;
    class TaskManager;

    /**
     * @enum RenderGraphResourceState
     * @brief 논리 리소스 접근 상태입니다. 그래프가 상태 전이(배리어)를 추론하는 데 씁니다.
     */
    enum class RenderGraphResourceState : uint8
    {
        Undefined, ///< 쓰지 않음 · 정의되지 않음
        Read,      ///< 패스 입력(읽기)
        Write,     ///< 패스 출력(쓰기)
    };

    /**
     * @struct RenderGraphBarrier
     * @brief 그래프가 **추론한** 상태 전이 하나입니다. "이 자원을 이 상태로 바꿔야 한다" 를 뜻합니다.
     * @details 예전에는 웨이브가 읽고 쓰는 자원 **이름을 모두** 넘겼고, 받는 쪽이 그것을 그대로 전이 호출로
     *          옮겼습니다. 같은 자원을 다섯 패스가 읽으면 다섯 번 불렀고, 이미 그 상태인 것도 다시 불렀습니다.
     *          이제 그래프가 자기가 들고 있는 상태와 비교해서 **실제로 바뀌는 것만** 냅니다.
     *
     *          `_before` 는 진단용입니다. 어디서 무엇이 어떻게 바뀌었는지 로그 · 테스트가 보려면 필요합니다.
     */
    struct RenderGraphBarrier
    {
        hashed_string            _resource;
        RenderGraphResourceState _before{ RenderGraphResourceState::Undefined };
        RenderGraphResourceState _after{ RenderGraphResourceState::Undefined };
    };

    /**
     * @struct RenderGraphPassContext
     * @brief execute() 중 패스 콜백에 넘기는 컨텍스트입니다.
     */
    struct RenderGraphPassContext
    {
        hashed_string                _passName; ///< 실행 중인 패스 이름
        const vector<hashed_string>* _pListInputs{ nullptr };
        const vector<hashed_string>* _pListOutputs{ nullptr };
        IRHICommandList*             _pCmdList{ nullptr }; ///< (선택) 병렬 기록 때의 활성 패스 커맨드 리스트
    };

    using RenderGraphPassExecuteFn = Delegate<void( const RenderGraphPassContext& )>;

    /**
     * @struct RenderGraphWaveContext
     * @brief 한 웨이브를 기록하기 **직전에**(직렬 경로에서는 패스마다) 단일 스레드에서 한 번 넘기는 컨텍스트입니다.
     * @details 이 웨이브를 위해 **실제로 필요한 전이만** 들어 있습니다. 받는 쪽은 이름을 텍스처로 풀어
     *          `prepareTextureForShaderRead` / `prepareTextureForRenderTarget` 을 미리 불러 둡니다.
     *          그러면 패스 콜백은 이미 맞는 상태를 보게 되어 기록 중에 리소스 상태를 바꾸지 않습니다.
     */
    struct RenderGraphWaveContext
    {
        /// @brief 이 웨이브 전에 걸어야 할 전이입니다. 중복도, 이미 맞는 상태도 없습니다(그래프가 걸러 냅니다).
        const vector<RenderGraphBarrier>* _pListBarrier{ nullptr };
        /// @brief 배리어를 기록할 커맨드 리스트입니다. 병렬 경로는 웨이브 첫 패스의 리스트(앞머리), 직렬 경로는 그 패스의 리스트입니다.
        IRHICommandList* _pCmdList{ nullptr };
    };

    using RenderGraphWavePrologueFn = Delegate<void( const RenderGraphWaveContext& )>;

    /**
     * @struct RenderGraphExecutionContext
     * @brief execute() 를 부를 때 임시 상태와 메모리를 관리하는 컨텍스트입니다.
     */
    struct RenderGraphExecutionContext
    {
        vector<pair<hashed_string, RenderGraphResourceState>> _listResourceState;
        unordered_map<hashed_string, size_t>                  _mapResourceToIndex;
        uint32                                                _lastTransitionCount{ 0 };

        /**
         * @brief 다음 execute() 호출 전에 상태를 초기화합니다(capacity 를 다시 써서 할당을 만들지 않습니다).
         * @details **프레임마다 잊는 것은 일부러입니다.** 전이는 그래프 밖에서도 일어납니다. 패스가 선언하지
         *          않은 텍스처를 registerPassTexture 로 걸거나, 리드백 · 에디터 블릿이 상태를 바꿉니다.
         *          지난 프레임의 믿음을 이어 가면 그런 자리에서 **필요한 배리어를 건너뜁니다**. 그래서
         *          프레임 시작에는 아무것도 모르는 것으로 치고, 첫 사용마다 전이를 냅니다. 진짜 상태를 아는
         *          것은 백엔드이고(DX12 는 상태가 같으면 배리어를 안 쏩니다) 여기서 버는 것은 **한 프레임
         *          안의 중복**입니다. 같은 자원을 세 패스가 읽어도 전이는 한 번입니다.
         */
        void reset()
        {
            _listResourceState.clear();
            _mapResourceToIndex.clear();
            _lastTransitionCount = 0;
        }

        /**
         * @brief 리소스 상태를 전이하고 **이전 상태**를 반환합니다(O(1) 해시 맵).
         * @details 반환값이 desired 와 같으면 이미 그 상태라 배리어를 낼 것이 없습니다. 처음 보는 자원은
         *          Undefined 였던 것으로 칩니다. 그 첫 전이는 실제로 필요합니다.
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
     * @brief Render Graph 안의 렌더 패스 노드 하나입니다.
     */
    struct RenderGraphNode
    {
        hashed_string            _name;             ///< 렌더 패스 고유 이름(해시)
        vector<hashed_string>    _listInput;        ///< 입력으로 읽는 자원 해시 목록
        vector<hashed_string>    _listOutput;       ///< 출력으로 만드는 자원 해시 목록
        RenderGraphPassExecuteFn _execute;          ///< 컴파일된 순서대로 부르는 패스 콜백(선택)
        bool                     _bCulled{ false }; ///< 쓰이지 않아 컬링됐는지 여부
    };

    /**
     * @struct RenderGraphResourceLifetime
     * @brief 리소스 하나의 사용 구간입니다(처음 · 마지막 사용 패스, 읽힘 · 쓰임 여부). 트랜지언트 앨리어싱 계산의 바탕이 되는 정보입니다.
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
     * @brief 렌더 패스 사이의 자원 의존 관계를 만들고 위상 정렬로 실행 순서를 정하는 프레임워크입니다.
     */
    class SW_API RenderGraph
    {
    public:
        /** @brief 빈 그래프로 만듭니다. */
        RenderGraph();
        /** @brief 병렬 기록용 커맨드 리스트를 놓습니다. 디바이스가 살아 있을 때 `releaseCommandLists` 로 먼저 놓아야 합니다. */
        ~RenderGraph();

        /** @brief 복사를 금지합니다. */
        RenderGraph( const RenderGraph& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderGraph& operator=( const RenderGraph& ) = delete;
        /** @brief 이동 생성자입니다. */
        RenderGraph( RenderGraph&& ) noexcept;
        /** @brief 이동 대입입니다. */
        RenderGraph& operator=( RenderGraph&& ) noexcept;

        /**
         * @brief 병렬 기록이 노드마다 들고 있는 커맨드 리스트를 놓습니다. 디바이스를 바꾸거나 놓기 **전에** 부릅니다.
         * @details 리스트는 프레임을 넘어 다시 씁니다. 예전에는 패스마다 프레임마다 새로 만들었습니다(래퍼 + 백엔드 할당).
         *          `clear()` 도 이것을 부릅니다.
         */
        void releaseCommandLists();

        /**
         * @brief 렌더 그래프에 새 렌더 패스 노드를 더합니다.
         * @param passName 렌더 패스 이름(hashed_string)
         * @param listInput 패스가 읽을 입력 자원 목록
         * @param listOutput 패스가 쓸 출력 자원 목록
         * @param execute 위상 정렬 순서로 실행할 때 부를 콜백(바인딩되지 않았으면 건너뜀)
         */
        void addPass( hashed_string passName, vector<hashed_string> listInput = {}, vector<hashed_string> listOutput = {},
                      RenderGraphPassExecuteFn execute = {} );

        /**
         * @brief 웨이브를 기록하기 직전(직렬 경로에서는 패스마다)에 부를 콜백을 등록합니다(배리어 발행용).
         * @details 등록하지 않으면 배리어를 내지 않고 상태만 추적합니다. 직렬 경로도 같은 콜백으로 배리어를 받습니다.
         */
        void setWavePrologue( const RenderGraphWavePrologueFn& prologue ) { _wavePrologue = prologue; }

        /**
         * @brief 렌더 패스 사이의 의존성을 검사하고 위상 정렬합니다.
         * @return 성공하면 true 입니다.
         */
        bool compile();

        /**
         * @brief 컴파일된 위상 순서로 패스 콜백을 실행합니다.
         * @details 패스마다 필요한 상태 전이를 **추론해서** 웨이브 콜백에 넘긴 뒤 패스를 부릅니다.
         *          실행 순서가 비어 있으면 compile() 을 한 번 시도합니다.
         * @param context 실행 중 자원 상태를 관리하는 임시 메모리 컨텍스트
         * @param pCmdList (선택) 배리어를 기록할 커맨드 리스트. 직렬 경로는 패스가 기록하는 리스트와
         *                 **같은 것**을 넘겨야 합니다. 배리어가 그 패스 바로 앞에 들어가야 하기 때문입니다.
         *                 nullptr 이어도 추론한 전이는 웨이브 콜백에 넘깁니다. 기록할 리스트가 없으니 받는 쪽이
         *                 기록하지 않을 뿐입니다(GPU 없는 테스트가 추론만 따로 볼 수 있습니다).
         * @return 실행에 성공하면 true 입니다(사이클 · 컴파일 실패면 false).
         */
        bool execute( RenderGraphExecutionContext& context, IRHICommandList* pCmdList = nullptr );

        /**
         * @brief TaskManager 워커 스레드로 렌더 패스 명령을 병렬로 기록하고 제출합니다.
         * @param context 실행 중 자원 상태를 관리하는 임시 메모리 컨텍스트
         * @param pTaskManager 백그라운드 태스크 매니저(nullptr 이면 직렬 execute() 로 폴백)
         * @param pDevice RHI 디바이스(커맨드 리스트 생성 · 제출용)
         * @return 실행에 성공하면 true 입니다.
         */
        bool executeParallel( RenderGraphExecutionContext& context, TaskManager* pTaskManager, IRHIDevice* pDevice );

        /**
         * @brief 최종 대상 출력에 닿지 않는, 쓰이지 않는 패스를 컬링합니다.
         * @param targetResourceName 최종 대상 리소스 해시 이름
         */
        void cullUnusedPasses( hashed_string targetResourceName );

        /**
         * @brief 여러 최종 대상 출력 어디에도 닿지 않는, 쓰이지 않는 패스를 한꺼번에 컬링합니다.
         * @param listRootOutput 최종 루트 대상 리소스 해시 목록
         * @param pOutListCulledPass (선택) 컬링된 패스 이름들을 받을 출력 파라미터
         */
        void cullUnreferencedPasses( const vector<hashed_string>& listRootOutput, vector<hashed_string>* pOutListCulledPass = nullptr );

        /** @brief 컴파일된 위상 정렬 패스 실행 순서를 반환합니다. */
        const vector<hashed_string>& getExecutionOrder() const { return _listCompiledExecutionOrder; }

        /**
         * @brief 컴파일된 실행 순서를 의존성 웨이브(레벨) 단위로 묶어 반환합니다.
         * @details 같은 웨이브 안의 패스들은 서로 입출력 의존이 없어(Kahn 위상 정렬의 같은 BFS 레벨)
         *          안전하게 동시에(병렬로) 기록할 수 있습니다. 웨이브 사이에는 순서를 지켜야 합니다.
         *          executeParallel() 이 이 구조를 써서 웨이브 안에서만 병렬 기록하고 웨이브 경계에서
         *          동기화합니다.
         */
        const vector<vector<hashed_string>>& getExecutionWaves() const { return _listCompiledWave; }

        /** @brief 그래프의 총 패스 노드 수를 반환합니다. */
        uint32 getNodeCount() const { return static_cast<uint32>( _listNode.size() ); }

        /** @brief 특정 렌더 패스가 컬링되었는지 반환합니다. */
        bool isPassCulled( hashed_string passName ) const;

        /**
         * @brief 각 리소스의 수명(처음 ~ 마지막 사용 패스 인덱스)입니다. compile() 이 계산해 둔 것을 반환합니다.
         * @details 예전에는 부를 때마다 다시 계산했고 아무도 부르지 않았습니다. 지금은 compile() 이 채우면서
         *          `buildResourceLifetimes` 가 그 자리에서 쓰이지 않는 · 쓰인 적 없는 자원을 검사합니다. 수명이 그래프의 산출물이 됐습니다.
         */
        const vector<RenderGraphResourceLifetime>& getResourceLifetimes() const { return _listResourceLifetime; }
        /** @brief 리소스 하나의 수명입니다. 그래프가 모르는 이름이면 nullptr 입니다. */
        const RenderGraphResourceLifetime* findResourceLifetime( hashed_string name ) const;

        /** @brief 디버깅 · 시각화를 위해 지금 그래프 구성을 Mermaid 다이어그램 텍스트로 내보냅니다. */
        string exportToMermaid() const;

        /** @brief 디버깅을 위해 지금 그래프 구성을 Graphviz DOT 다이어그램 텍스트로 내보냅니다. */
        string exportToDot() const;

        /** @brief 모든 패스와 컴파일 상태를 초기화합니다. */
        void clear();

    private:
        vector<RenderGraphNode> _listNode;
        vector<hashed_string>   _listCompiledExecutionOrder;
        /** @brief _listCompiledExecutionOrder 와 같은 순서 · 내용을 웨이브(의존성 레벨) 단위로 묶은 것입니다. */
        vector<vector<hashed_string>> _listCompiledWave;

        // 핫패스에서 할당하지 않기 위한 캐시
        unordered_map<hashed_string, size_t> _mapNameToIndex;

        /// @brief 웨이브(직렬이면 패스)를 기록하기 직전에 부르는 콜백입니다. setWavePrologue 참고.
        RenderGraphWavePrologueFn _wavePrologue;
        /// @brief 이번 웨이브(직렬이면 이번 패스)의 배리어입니다. 프레임마다 다시 써서 할당을 반복하지 않습니다.
        vector<RenderGraphBarrier> _listWaveBarrier;
        /// @brief 배리어를 추리는 동안 쓰는 스크래치입니다: 자원 이름 → `_listWaveBarrier` 인덱스(웨이브 안 중복 제거).
        unordered_map<hashed_string, size_t> _mapWaveBarrierIndex;

        /** @brief 병렬 기록이 프레임마다 다시 쓰는 것입니다(패스 엔트리 목록과 노드별 커맨드 리스트). 완전한 타입은 cpp 에만 있습니다. */
        struct ParallelScratch;
        unique_ptr<ParallelScratch> _pParallelScratch;
        /// @brief compile() 이 계산한 리소스 수명입니다(처음 · 마지막 사용 패스와 읽힘 · 쓰임 여부).
        vector<RenderGraphResourceLifetime>  _listResourceLifetime;
        unordered_map<hashed_string, size_t> _mapResourceLifetimeIndex;

        /**
         * @brief 패스 하나가 필요로 하는 전이를 `_listWaveBarrier` 에 보탭니다.
         * @details 같은 웨이브 안에서 한 자원을 읽기와 쓰기로 동시에 요구할 일은 없습니다. compile() 이
         *          RAW/WAW 엣지로 그런 패스들을 다른 웨이브에 갈라 놓기 때문입니다. 그래도 들어오면 쓰기를
         *          택합니다(더 강한 상태입니다). 이미 그 상태인 자원은 아예 넣지 않습니다.
         */
        void appendPassBarriers( RenderGraphExecutionContext& context, const RenderGraphNode& node );
        /** @brief 추린 배리어를 웨이브 콜백에 넘깁니다. 콜백이 없거나 낼 것이 없으면 아무 일도 하지 않습니다. */
        void issueBarriers( IRHICommandList* pCmdList );
        /** @brief compile() 끝에서 리소스 수명을 채우고, 쓰이지 않는 · 쓰인 적 없는 자원을 로그로 알립니다. */
        void buildResourceLifetimes();
    };
} // namespace sw
