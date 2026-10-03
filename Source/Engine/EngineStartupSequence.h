/**
 * @file EngineStartupSequence.h
 * @brief 엔진 기동 단계를 의존 선언(`EngineStartupStepList.xxx`)으로 위상 정렬해 초기화하고, 그 역순으로 종료 · 해제합니다. 표는 그 순서대로 적힙니다.
 * @details 언리얼 `FSubsystemCollectionBase::InitializeDependency` 와 같은 자리입니다 — 각 단계가 먼저 서야 하는 단계를
 *          적고, 초기화는 그 그래프의 위상 순서, 종료(`Deinitialize`)와 객체 해제는 역순입니다. 순서 지식은 표의 의존 칸 하나에 있고
 *          호스트(`EngineLoop` · 시험 하네스)는 단계마다 구조체 하나(`<단계>StartupStep`)로 본문만 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 기동 단계입니다. 값은 표(`EngineStartupStepList.xxx`)의 줄 순서입니다. */
    enum class EngineStartupStep : uint8
    {
#define SW_ENGINE_STARTUP_STEP( Name, ... ) Name,
#include "Engine/EngineStartupStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        Count
    };

    /** @brief 단계 하나의 초기화 결과입니다. */
    enum class EngineStartupResult : uint8
    {
        Succeeded,      ///< 다음 단계로 갑니다
        Failed,         ///< 기동을 멈춥니다. 이미 초기화한 단계는 `shutdownAll` 이 역순으로 내립니다
        SkipDependents, ///< 이 단계는 초기화됐지만, 이 단계에 (간접으로라도) 의존하는 단계는 돌지 않습니다(헤드리스 작업)
    };

    /** @brief 의존 그래프의 노드 하나입니다. */
    struct EngineStartupNode
    {
        const utf8* _pName;           ///< 단계 이름
        const utf8* _pDependencyText; ///< 먼저 서야 하는 단계 이름들(쉼표 · 중괄호 · 공백으로 구분 — 표는 `{ A, B }`)
    };

    /** @brief `EngineStartupSequence::computeGraph` 의 결과입니다. 인덱스는 노드 목록의 자리입니다. */
    struct EngineStartupGraph
    {
        vector<uint32>         _listOrder;      ///< 초기화 순서(위상 순서). 준비된 노드가 여럿이면 이름이 앞인 것이 먼저입니다
        vector<vector<uint32>> _listDependency; ///< 노드마다 먼저 서야 하는 노드
    };

    /**
     * @brief 단계 본문의 기본값입니다. 아무것도 하지 않고 성공합니다.
     * @details 호스트는 표의 줄마다 `<단계>StartupStep` 구조체를 두고, 이것을 상속해 필요한 함수만 다시 정의합니다(이름 가림).
     *          언리얼 subsystem 의 `Initialize` / `Deinitialize` 와 같은 자리이고, 해제(`destroy`)가 하나 더 있습니다.
     * @tparam THost 본문을 돌리는 호스트(`EngineLoop` · 시험 하네스)입니다.
     */
    template <class THost>
    struct EngineStartupStepDefaults
    {
        /** @brief 단계를 세웁니다. 위상 순서로 불립니다. */
        static EngineStartupResult initialize( THost& ) { return EngineStartupResult::Succeeded; }
        /** @brief 세운 단계를 내립니다. **초기화한 단계만** 역순으로 불리고, 이때 모든 단계의 객체는 아직 살아 있습니다. */
        static void shutdown( THost& ) {}
        /**
         * @brief 단계가 소유한 객체를 해제합니다.
         * @details 모든 단계의 `shutdown` 뒤에 **표의 모든 단계**에 역순으로 불립니다 — 기동이 어디서 멈췄든 같습니다. 단계의 객체는
         *          부트스트랩이 미리 만들었거나(서비스) 실패한 초기화가 반쯤 만들었을 수 있기 때문입니다. 그래서 만들지 않은 객체에도
         *          불리고, 본문은 null 안전해야 합니다.
         */
        static void destroy( THost& ) {}
    };

    /** @brief 단계 하나의 본문 셋입니다. 호스트 타입을 지운 포인터를 받습니다(`EngineStartupStepTable` 이 채웁니다). */
    struct EngineStartupStepEntry
    {
        EngineStartupResult ( *_pInitialize )( void* pHost ); ///< `<단계>StartupStep::initialize`
        void ( *_pShutdown )( void* pHost );                  ///< `<단계>StartupStep::shutdown`
        void ( *_pDestroy )( void* pHost );                   ///< `<단계>StartupStep::destroy`
    };

    /** @brief 단계 구조체의 정적 함수를 `EngineStartupStepEntry` 의 모양으로 잇습니다. */
    template <class THost, class TStep>
    struct EngineStartupStepThunk
    {
        static EngineStartupResult initialize( void* pHost ) { return TStep::initialize( *static_cast<THost*>( pHost ) ); }
        static void                shutdown( void* pHost ) { TStep::shutdown( *static_cast<THost*>( pHost ) ); }
        static void                destroy( void* pHost ) { TStep::destroy( *static_cast<THost*>( pHost ) ); }

        static constexpr EngineStartupStepEntry kEntry{ &initialize, &shutdown, &destroy }; ///< 표의 칸 하나
    };

    /**
     * @brief 호스트의 단계 구조체(`THost::<단계>StartupStep`)로 만든 본문 표입니다. 자리는 `EngineStartupStep` 값입니다.
     * @details 표(`EngineStartupStepList.xxx`)의 줄마다 구조체가 하나 있어야 하고, 빠지면 컴파일 오류입니다. 단계마다 `switch` 를
     *          두지 않으므로 단계를 더하는 일은 표 한 줄과 호스트마다 구조체 하나입니다. 구조체를 private 으로 둔 호스트는
     *          `template <class> friend struct EngineStartupStepTable;` 로 이 표에만 보입니다.
     */
    template <class THost>
    struct EngineStartupStepTable
    {
        static constexpr EngineStartupStepEntry kArrEntry[] = {
#define SW_ENGINE_STARTUP_STEP( Name, ... ) EngineStartupStepThunk<THost, typename THost::Name##StartupStep>::kEntry,
#include "Engine/EngineStartupStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        };
        static_assert( sizeof( kArrEntry ) / sizeof( kArrEntry[0] ) == static_cast<size_t>( EngineStartupStep::Count ),
                       "Startup step body table must have one row per EngineStartupStep" );
    };

    /**
     * @class EngineStartupSequence
     * @brief 기동 단계 표를 정렬해 들고, 호스트의 단계 구조체로 초기화 · 종료 · 해제를 돌립니다.
     */
    class SW_API EngineStartupSequence
    {
    public:
        /** @brief 표를 정렬합니다. 모르는 이름 · 순환이면 `initializeAll` 이 오류를 알리고 실패합니다. */
        EngineStartupSequence();

        /**
         * @brief 위상 순서로 단계마다 `THost::<단계>StartupStep::initialize` 를 부릅니다.
         * @details `Failed` 면 거기서 멈추고 false 입니다. `SkipDependents` 면 그 단계에 의존하는 단계를 건너뜁니다.
         *          @p host 를 기억해 두었다가 `shutdownAll` · `destroyAll` 이 같은 호스트로 부릅니다. 호스트는 그때까지 살아 있어야 합니다.
         * @return 표가 유효하고 실패한 단계가 없으면 true 입니다.
         */
        template <class THost>
        [[nodiscard]] bool initializeAll( THost& host )
        {
            return initializeAllInternal( EngineStartupStepTable<THost>::kArrEntry, &host );
        }
        /** @brief 초기화한 단계를 역순으로 종료합니다(`shutdown`). 두 번 불러도 됩니다. */
        void shutdownAll();
        /**
         * @brief 표의 모든 단계를 역순으로 해제합니다(`destroy`). 아직 종료하지 않은 단계가 있으면 먼저 `shutdownAll` 합니다.
         * @details 초기화한 단계만이 아니라 **모든 단계**입니다 — 실패한 단계, 건너뛴 단계(`SkipDependents`), 닿지 못한 단계도 해제합니다
         *          (`EngineStartupStepDefaults::destroy`). `initializeAll` 을 부른 적이 없으면 아무것도 하지 않습니다.
         */
        void destroyAll();

        /**
         * @brief @p step 에 (간접으로라도) 의존하는 **초기화된** 단계를 역순으로 종료합니다. @p step 자신은 그대로 둡니다.
         * @details 기동 중에 한 단계를 갈아 끼울 때 쓴다(백엔드 교체: RHI 의 디바이스를 다시 만든다). 내린 단계는
         *          `restartStoppedSteps` 가 같은 본문으로 다시 세운다 — 교체 경로가 기동 본문을 손으로 베끼지 않는다.
         */
        void shutdownDependentsOf( EngineStartupStep step );
        /**
         * @brief `shutdownDependentsOf` 가 내린 단계를 위상 순서로 다시 초기화합니다(`initialize`). 본문은 이미 있는 객체를 다시 쓴다.
         * @return 모두 다시 섰으면 true 입니다. 실패한 단계에서 멈추고, 그 단계와 나머지는 내린 채로 남습니다(종료 대상에서도 빠진다).
         */
        [[nodiscard]] bool restartStoppedSteps();
        /** @brief `shutdownDependentsOf` 가 내리고 아직 다시 세우지 않은 단계입니다(초기화 순서). */
        const vector<EngineStartupStep>& getStoppedSteps() const { return _listStopped; }

        /** @brief 표의 초기화 순서입니다(본문을 돌리지 않아도 나옵니다). */
        const vector<EngineStartupStep>& getInitializeOrder() const { return _listOrder; }
        /** @brief 지금 초기화돼 있는 단계입니다(초기화한 순서). */
        const vector<EngineStartupStep>& getInitializedSteps() const { return _listInitialized; }
        /** @brief 표 오류(모르는 이름 · 순환)입니다. 없으면 빈 글입니다. */
        const string& getError() const { return _error; }

        /** @brief 단계 이름(표의 철자)입니다. */
        static const utf8* getStepName( EngineStartupStep step );
        /** @brief 표 그대로의 노드 목록입니다(줄 순서). */
        static vector<EngineStartupNode> makeStepNodes();
        /**
         * @brief 노드 목록을 위상 정렬합니다. 준비된 노드가 여럿이면 이름이 앞인 것이 먼저입니다(목록 순서는 보지 않습니다).
         * @return 같은 이름 · 모르는 의존 · 순환이면 false 이고 @p outError 에 무엇인지 적습니다.
         */
        [[nodiscard]] static bool computeGraph( const vector<EngineStartupNode>& listNode, EngineStartupGraph& outGraph, string& outError );

    private:
        /** @brief `initializeAll` 의 본체입니다. @p pArrEntry 는 `EngineStartupStep::Count` 칸입니다. */
        [[nodiscard]] bool initializeAllInternal( const EngineStartupStepEntry* pArrEntry, void* pHost );

    private:
        vector<EngineStartupStep>     _listOrder;       ///< 초기화 순서
        vector<vector<uint32>>        _listDependency;  ///< 단계마다 먼저 서야 하는 단계(표의 자리)
        vector<EngineStartupStep>     _listInitialized; ///< 초기화한 단계(종료가 역순으로 돈다)
        vector<EngineStartupStep>     _listStopped;     ///< `shutdownDependentsOf` 가 내린 단계(초기화 순서)
        const EngineStartupStepEntry* _pArrEntry;       ///< `initializeAll` 이 받은 호스트의 본문 표
        void*                         _pHost;           ///< 본문에 넘길 호스트
        string                        _error;           ///< 표 오류
    };
} // namespace sw
