/**
 * @file EngineInitSequence.h
 * @brief 엔진 기동 단계를 의존 선언(`EngineInitStepList.xxx`)으로 위상 정렬해 초기화하고, 그 역순으로 종료 · 해제합니다.
 * @details 언리얼 `FSubsystemCollectionBase::InitializeDependency` 와 같은 자리입니다 — 각 단계가 먼저 서야 하는 단계를
 *          적고, 초기화는 그 그래프의 위상 순서(동점은 이름 순), 종료(`Deinitialize`)와 객체 해제는 역순입니다. 순서 지식은 표의 의존 칸
 *          하나에 있고 표의 줄 위치는 뜻이 없습니다. 호스트(`EngineLoop` · 테스트 하네스)는 본문 모음 하나에 `initialize<단계>` ·
 *          `shutdown<단계>` · `destroy<단계>` 중 필요한 정적 함수만 둡니다(`EngineInitNoStepBody`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/MemoryTag.h"

namespace sw
{
    /** @brief 기동 단계입니다. 값은 표(`EngineInitStepList.xxx`)의 줄 자리일 뿐이고 순서가 아닙니다(순서는 `getInitializeOrder`). */
    enum class EngineInitStep : uint8
    {
#define SW_ENGINE_STARTUP_STEP( Name, ... ) Name,
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        Count
    };

    /** @brief 기동 단계가 도는 호스트입니다(표의 셋째 칸). 비트라 `All` 은 둘 다입니다. */
    enum class EngineInitTarget : uint8
    {
        Client = 1 << 0, ///< 창 · 렌더러 · 플레이어 설정이 있는 호스트(App · 에디터 · 테스트 하네스)
        Server = 1 << 1, ///< 전용 서버(Server 실행 파일)
        All    = Client | Server,
    };

    /** @brief 단계 하나의 초기화 결과입니다. */
    enum class EngineInitResult : uint8
    {
        Succeeded,      ///< 다음 단계로 갑니다
        Failed,         ///< 기동을 멈춥니다. 이미 초기화한 단계는 `shutdownAll` 이 역순으로 내립니다
        SkipDependents, ///< 이 단계는 초기화됐지만, 이 단계에 (간접으로라도) 의존하는 단계는 돌지 않습니다(헤드리스 작업)
    };

    /** @brief 의존 그래프의 노드 하나입니다. */
    struct EngineInitNode
    {
        const utf8* _pName;           ///< 단계 이름
        const utf8* _pDependencyText; ///< 먼저 서야 하는 단계 이름들(쉼표 · 중괄호 · 공백으로 구분 — 표는 `{ A, B }`)
    };
} // namespace sw

namespace sw
{
    /** @brief `EngineInitSequence::computeGraph` 의 결과입니다. 인덱스는 노드 목록의 자리입니다. */
    struct EngineInitGraph
    {
        vector<uint32>         _listOrder;      ///< 초기화 순서(위상 순서). 준비된 노드가 여럿이면 이름이 앞인 것이 먼저입니다
        vector<vector<uint32>> _listDependency; ///< 노드마다 먼저 서야 하는 노드
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 단계 본문이 없는 자리입니다. 호스트의 본문 모음이 이것을 상속하고, 필요한 함수만 같은 이름으로 정의합니다(이름 가림).
     * @details 표의 줄마다 `initialize<단계>` · `shutdown<단계>` · `destroy<단계>` 세 이름을 null 로 둡니다. 본문 모음이
     *          `static EngineInitResult initializeScene( THost& )` 를 정의하면 그것이 걸리고, 정의하지 않은 자리는 아무것도 하지 않습니다
     *          (초기화는 성공으로 칩니다). 세 본문의 뜻은 언리얼 subsystem 의 `Initialize` / `Deinitialize` 에 해제가 하나 더 붙은 것입니다.
     *          - `initialize<단계>( THost& )` → `EngineInitResult`: 단계를 세운다. 위상 순서로 불린다.
     *          - `shutdown<단계>( THost& )`: 세운 단계를 내린다. **초기화한 단계만** 역순으로 불리고, 이때 모든 단계의 객체는 아직 살아 있다.
     *          - `destroy<단계>( THost& )`: 단계가 소유한 객체를 해제한다. 모든 종료 뒤에 **모든 단계**에 역순으로 불린다(기동이 어디서 멈췄든).
     *            단계의 객체는 부트스트랩이 미리 만들었거나 실패한 초기화가 반쯤 만들었을 수 있어서, 본문은 null 안전해야 한다.
     * @note 이름이 틀린 본문(`initializeScnee`)은 어느 자리에도 걸리지 않습니다. 시그니처가 틀린 본문은 컴파일 오류입니다.
     */
    struct EngineInitNoStepBody
    {
#define SW_ENGINE_STARTUP_STEP( Name, ... )                          \
    static constexpr decltype( nullptr ) initialize##Name = nullptr; \
    static constexpr decltype( nullptr ) shutdown##Name   = nullptr; \
    static constexpr decltype( nullptr ) destroy##Name    = nullptr;
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
    };
} // namespace sw

namespace sw
{
    /** @brief 단계 하나의 본문 셋입니다. 호스트 타입을 지운 포인터이고, null 이면 그 본문이 없습니다(`EngineInitStepTable` 이 채웁니다). */
    struct EngineInitStepEntry
    {
        using InitializeFunction = EngineInitResult ( * )( void* pHost );
        using StepFunction       = void ( * )( void* pHost );

        InitializeFunction _pInitialize; ///< `initialize<단계>`
        StepFunction       _pShutdown;   ///< `shutdown<단계>`
        StepFunction       _pDestroy;    ///< `destroy<단계>`
    };
} // namespace sw

namespace sw
{
    /** @brief 본문 모음의 정적 함수(또는 `EngineInitNoStepBody` 의 null)를 `EngineInitStepEntry` 의 칸으로 바꿉니다. */
    template <class THost>
    struct EngineInitStepBinder
    {
        template <auto pInitialize>
        static EngineInitResult initialize( void* pHost )
        {
            return pInitialize( *static_cast<THost*>( pHost ) );
        }
        template <auto pFunction>
        static void call( void* pHost )
        {
            pFunction( *static_cast<THost*>( pHost ) );
        }

        template <auto pInitialize>
        static constexpr EngineInitStepEntry::InitializeFunction bindInitialize()
        {
            if constexpr ( std::is_null_pointer_v<decltype( pInitialize )> )
                return nullptr;
            else
                return &initialize<pInitialize>;
        }
        template <auto pFunction>
        static constexpr EngineInitStepEntry::StepFunction bindCall()
        {
            if constexpr ( std::is_null_pointer_v<decltype( pFunction )> )
                return nullptr;
            else
                return &call<pFunction>;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 호스트의 본문 모음(`TBody::initialize<단계>` …)으로 만든 본문 표입니다. 자리는 `EngineInitStep` 값입니다.
     * @details 단계마다 타입을 두지 않습니다 — 표 한 줄에 본문 이름 셋이 따라오고, 본문 모음이 그중 필요한 것만 정의합니다.
     *          `EngineInitNoStepBody` 를 상속하지 않은 본문 모음은 모든 자리를 정의해야 합니다(빠지면 컴파일 오류).
     */
    template <class THost, class TBody>
    struct EngineInitStepTable
    {
        using Binder = EngineInitStepBinder<THost>;

        static constexpr EngineInitStepEntry kArrEntry[] = {
#define SW_ENGINE_STARTUP_STEP( Name, ... ) \
    { Binder::template bindInitialize<TBody::initialize##Name>(), Binder::template bindCall<TBody::shutdown##Name>(), Binder::template bindCall<TBody::destroy##Name>() },
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        };
        static_assert( sizeof( kArrEntry ) / sizeof( kArrEntry[0] ) == static_cast<size_t>( EngineInitStep::Count ),
                       "Startup step body table must have one row per EngineInitStep" );
    };
} // namespace sw

namespace sw
{
    /**
     * @class EngineInitSequence
     * @brief 기동 단계 표를 정렬해 들고, 호스트의 단계 구조체로 초기화 · 종료 · 해제를 돌립니다.
     */
    class SW_API EngineInitSequence
    {
    public:
        /** @brief 표를 정렬합니다. 모르는 이름 · 순환이면 `initializeAll` 이 오류를 알리고 실패합니다. */
        EngineInitSequence();

        /**
         * @brief 위상 순서로 단계마다 `TBody::initialize<단계>` 를 부릅니다(정의하지 않은 단계는 성공으로 칩니다).
         * @details `Failed` 면 거기서 멈추고 false 입니다. `SkipDependents` 면 그 단계에 의존하는 단계를 건너뜁니다.
         *          @p target 과 겹치지 않는 대상의 단계는 부르지 않고 건너뜁니다(전용 서버의 RHI · 렌더러 · 플레이어 설정).
         *          @p host 를 기억해 두었다가 `shutdownAll` · `destroyAll` 이 같은 호스트로 부릅니다. 호스트는 그때까지 살아 있어야 합니다.
         * @tparam TBody 본문 모음입니다(`EngineInitNoStepBody` 를 상속하고 필요한 `initialize<단계>` · `shutdown<단계>` · `destroy<단계>` 만 정의).
         * @param target 이 호스트의 대상. App · 테스트 하네스는 `Client`, 전용 서버는 `Server` 입니다.
         * @return 표가 유효하고 실패한 단계가 없으면 true 입니다.
         */
        template <class TBody, class THost>
        [[nodiscard]] bool initializeAll( THost& host, EngineInitTarget target = EngineInitTarget::Client )
        {
            return initializeAllInternal( EngineInitStepTable<THost, TBody>::kArrEntry, &host, target );
        }
        /** @brief 초기화한 단계를 역순으로 종료합니다(`shutdown`). 두 번 불러도 됩니다. */
        void shutdownAll();
        /**
         * @brief 표의 모든 단계를 초기화 순서의 역순으로 해제합니다(`destroy<단계>`). 아직 종료하지 않은 단계가 있으면 먼저 `shutdownAll` 합니다.
         * @details 초기화한 단계만이 아니라 **모든 단계**입니다 — 실패한 단계, 건너뛴 단계(`SkipDependents`), 닿지 못한 단계도 해제합니다.
         *          `initializeAll` 을 부른 적이 없으면 아무것도 하지 않습니다.
         */
        void destroyAll();

        /**
         * @brief @p step 에 (간접으로라도) 의존하는 **초기화된** 단계를 역순으로 종료합니다. @p step 자신은 그대로 둡니다.
         * @details 기동 중에 한 단계를 갈아 끼울 때 쓴다(백엔드 교체: RHI 의 디바이스를 다시 만든다). 내린 단계는
         *          `restartStoppedSteps` 가 같은 본문으로 다시 세운다 — 교체 경로가 기동 본문을 손으로 베끼지 않는다.
         */
        void shutdownDependentsOf( EngineInitStep step );
        /**
         * @brief `shutdownDependentsOf` 가 내린 단계를 위상 순서로 다시 초기화합니다(`initialize`). 본문은 이미 있는 객체를 다시 쓴다.
         * @return 모두 다시 섰으면 true 입니다. 실패한 단계에서 멈추고, 그 단계와 나머지는 내린 채로 남습니다(종료 대상에서도 빠진다).
         */
        [[nodiscard]] bool restartStoppedSteps();
        /** @brief `shutdownDependentsOf` 가 내리고 아직 다시 세우지 않은 단계입니다(초기화 순서). */
        const vector<EngineInitStep>& getStoppedSteps() const { return _listStopped; }

        /** @brief 의존에서 계산한 초기화 순서입니다(본문을 돌리지 않아도 나옵니다). 종료 · 해제는 이 역순입니다. */
        const vector<EngineInitStep>& getInitializeOrder() const { return _listOrder; }
        /** @brief 지금 초기화돼 있는 단계입니다(초기화한 순서). */
        const vector<EngineInitStep>& getInitializedSteps() const { return _listInitialized; }
        /** @brief 표 오류(모르는 이름 · 순환)입니다. 없으면 빈 글입니다. */
        const string& getError() const { return _error; }

        /** @brief 단계 이름(표의 철자)입니다. */
        static const utf8* getStepName( EngineInitStep step );
        /** @brief 단계 초기화가 할당하는 메모리의 용도(표의 둘째 칸)입니다. 초기화 · 재시작을 부르는 동안 이 태그가 걸립니다. */
        static MemoryTag getStepMemoryTag( EngineInitStep step );
        /** @brief 단계가 도는 호스트(표의 셋째 칸)입니다. */
        static EngineInitTarget getStepTarget( EngineInitStep step );
        /** @brief 표 그대로의 노드 목록입니다(줄 순서). */
        static vector<EngineInitNode> makeStepNodes();
        /**
         * @brief 노드 목록을 위상 정렬합니다. 준비된 노드가 여럿이면 이름이 앞인 것이 먼저입니다(목록 순서는 보지 않습니다).
         * @return 같은 이름 · 모르는 의존 · 순환이면 false 이고 @p outError 에 무엇인지 적습니다.
         */
        [[nodiscard]] static bool computeGraph( const vector<EngineInitNode>& listNode, EngineInitGraph& outGraph, string& outError );

    private:
        /** @brief `initializeAll` 의 본체입니다. @p pArrEntry 는 `EngineInitStep::Count` 칸입니다. */
        [[nodiscard]] bool initializeAllInternal( const EngineInitStepEntry* pArrEntry, void* pHost, EngineInitTarget target );

    private:
        vector<EngineInitStep>     _listOrder;       ///< 초기화 순서
        vector<vector<uint32>>     _listDependency;  ///< 단계마다 먼저 서야 하는 단계(표의 자리)
        vector<EngineInitStep>     _listInitialized; ///< 초기화한 단계(종료가 역순으로 돈다)
        vector<EngineInitStep>     _listStopped;     ///< `shutdownDependentsOf` 가 내린 단계(초기화 순서)
        const EngineInitStepEntry* _pArrEntry;       ///< `initializeAll` 이 받은 호스트의 본문 표
        void*                      _pHost;           ///< 본문에 넘길 호스트
        string                     _error;           ///< 표 오류
    };
} // namespace sw
