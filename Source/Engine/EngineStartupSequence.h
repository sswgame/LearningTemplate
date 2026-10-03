/**
 * @file EngineStartupSequence.h
 * @brief 엔진 기동 단계를 의존 선언(`EngineStartupStepList.xxx`)으로 위상 정렬해 초기화하고, 그 역순으로 종료합니다.
 * @details 언리얼 `FSubsystemCollectionBase::InitializeDependency` 와 같은 자리입니다 — 각 단계가 먼저 서야 하는 단계를
 *          적고, 초기화는 그 그래프의 위상 순서, 종료(`Deinitialize`)는 역순입니다. 순서 지식은 표의 의존 칸 하나에 있고
 *          호스트(`EngineLoop` · 시험 하네스)는 단계마다 본문만 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    /** @brief 기동 단계입니다. 값은 표(`EngineStartupStepList.xxx`)의 줄 순서입니다. */
    enum class EngineStartupStep : uint8
    {
#define SW_ENGINE_STARTUP_STEP( Name, Dependencies ) Name,
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
        const utf8* _pDependencyText; ///< 먼저 서야 하는 단계 이름들(공백으로 구분)
    };

    /** @brief `EngineStartupSequence::computeGraph` 의 결과입니다. 인덱스는 노드 목록의 자리입니다. */
    struct EngineStartupGraph
    {
        vector<uint32>         _listOrder;      ///< 초기화 순서(위상 순서). 준비된 노드가 여럿이면 목록 앞의 것이 먼저입니다
        vector<vector<uint32>> _listDependency; ///< 노드마다 먼저 서야 하는 노드
    };

    /**
     * @class EngineStartupSequence
     * @brief 기동 단계 표를 정렬해 들고, 호스트가 준 본문으로 초기화 · 종료를 돌립니다.
     */
    class SW_API EngineStartupSequence
    {
    public:
        using InitializeStepDelegate = Delegate<EngineStartupResult( EngineStartupStep )>;
        using ShutdownStepDelegate   = Delegate<void( EngineStartupStep )>;

        /** @brief 표를 정렬합니다. 모르는 이름 · 순환이면 `initializeAll` 이 오류를 알리고 실패합니다. */
        EngineStartupSequence();

        /**
         * @brief 위상 순서로 단계마다 @p initializeStep 을 부릅니다.
         * @details `Failed` 면 거기서 멈추고 false 입니다. `SkipDependents` 면 그 단계에 의존하는 단계를 건너뜁니다.
         *          종료는 **초기화한 단계만** 역순으로 @p shutdownStep 에 넘깁니다(`shutdownAll`).
         * @return 표가 유효하고 실패한 단계가 없으면 true 입니다.
         */
        [[nodiscard]] bool initializeAll( const InitializeStepDelegate& initializeStep, const ShutdownStepDelegate& shutdownStep );
        /** @brief 초기화한 단계를 역순으로 종료합니다. 두 번 불러도 됩니다. */
        void shutdownAll();

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
         * @brief 노드 목록을 위상 정렬합니다. 준비된 노드가 여럿이면 목록 앞의 것이 먼저입니다.
         * @return 같은 이름 · 모르는 의존 · 순환이면 false 이고 @p outError 에 무엇인지 적습니다.
         */
        [[nodiscard]] static bool computeGraph( const vector<EngineStartupNode>& listNode, EngineStartupGraph& outGraph, string& outError );

    private:
        vector<EngineStartupStep> _listOrder;       ///< 초기화 순서
        vector<vector<uint32>>    _listDependency;  ///< 단계마다 먼저 서야 하는 단계(표의 자리)
        vector<EngineStartupStep> _listInitialized; ///< 초기화한 단계(종료가 역순으로 돈다)
        ShutdownStepDelegate      _shutdownStep;    ///< `initializeAll` 이 받은 종료 본문
        string                    _error;           ///< 표 오류
    };
} // namespace sw
