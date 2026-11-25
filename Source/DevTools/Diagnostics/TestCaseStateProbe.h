/**
 * @file TestCaseStateProbe.h
 * @brief 단위 케이스 앞뒤의 프로세스 상태를 읽어, 케이스가 흘린 것을 이름으로 냅니다(`TestRunOptions::_onCaseBegin` · `_onCaseEnd`).
 * @details 시나리오 단계의 상태 지문(`AutomationStageFingerprint`)의 단위 실행기 자리다 — 흘린 상태(남은 비동기 로드 · 되돌리지 않은 전역 변수 · 떼지 않은 도메인)가 다음 케이스에
 *          오류 로그로 나타나 엉뚱한 케이스가 졌다. 흘린 그 케이스가 진다.
 *          케이스마다 드는 것은 수 몇 개와 해시 몇 개의 비교다 — 다를 때만 무엇이 다른지 글을 만든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    struct GlobalVariableInfo;
    class Subsystem;
} // namespace sw

namespace sw
{
    /** @brief 케이스 경계의 상태 읽기입니다. 실행(`TestRunner::run`)을 부르는 쪽이 값으로 듭니다. 테스트 스레드(게임 스레드)에서만 부릅니다. */
    class SW_DEVTOOLS_API TestCaseStateProbe
    {
    public:
        TestCaseStateProbe();

        /** @brief 케이스 앞의 상태를 찍습니다. */
        void beginCase();
        /**
         * @brief 케이스가 띄운 태스크가 끝나기를 기다린 뒤(그 로그도 이 케이스의 것이다) 앞과 다른 상태를 @p outListLeak 에 한 줄씩 채웁니다.
         * @details 흘린 것은 다음 케이스의 기준이 된다 — 다음 케이스가 같은 것으로 또 지지 않는다.
         */
        void endCase( vector<string>& outListLeak );

    private:
        /** @brief 늘면 샌 것인 수와, 다르면 샌 것인 해시입니다. */
        struct Snapshot
        {
            uint64 _subscriberCount{ 0 };
            uint64 _commandCount{ 0 };
            uint64 _pauseRequestCount{ 0 };
            uint64 _sceneCount{ 0 };
            uint64 _domainHash{ 0 };
            uint64 _tagHash{ 0 };
            uint64 _policyHash{ 0 };
            uint64 _variableHash{ 0 };
            uint32 _taskCount{ 0 };
        };

        [[nodiscard]] Snapshot capture();
        [[nodiscard]] uint64   hashVariables();
        /** @brief 전역 변수마다의 값 글을 지금 값으로 다시 찍고, 앞과 다른 변수를 @p pOutListLeak 에 채웁니다(nullptr 이면 찍기만). */
        void refreshVariableTexts( vector<string>* pOutListLeak );
        /** @brief 게임 인스턴스 상태가 남은 서브시스템(`Subsystem::findGameInstanceState`) 가운데 처음 본 것을 @p outListState 에 채웁니다. */
        void collectNewStates( vector<string>& outListState );

    private:
        Snapshot                    _before;
        vector<GlobalVariableInfo*> _listVariable;         ///< `_variableGeneration` 때의 변수 목록
        vector<string>              _listVariableText;     ///< `_listVariable` 과 같은 차례의 값 글(기준)
        vector<Subsystem*>          _listSubsystemScratch; ///< 케이스마다 다시 채운다(용량을 남긴다)
        vector<string>              _listDomainScratch;    ///< 위와 같다
        vector<string>              _listKnownState;       ///< 이미 본 서브시스템 상태(`<이름>: <상태>`)
        uint64                      _variableGeneration;   ///< 변수 등록 세대 — 바뀌면 목록을 다시 채운다
        uint8                       _bStarted : 1;         ///< 첫 케이스 앞의 상태를 기준으로 잡았다
    };
} // namespace sw
