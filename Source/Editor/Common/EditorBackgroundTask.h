/**
 * @file EditorBackgroundTask.h
 * @brief 워커가 만들고 게임 스레드가 가져가는 에디터 백그라운드 잡의 공통 뼈대입니다.
 *
 * @details 에디터의 파일 스캔과 로컬라이즈 로드 같은 잡은 모두 같은 규약을 쓴다. 게임 스레드가 `request()` 로 입력을
 *          싣고 세대를 올리면, 워커가 그 세대의 결과를 만들어 싣고, 게임 스레드가 `take()` 로 가져간다. 락, 세대 확인,
 *          플래그 전환을 잡마다 복사하면 한 곳만 틀려도 결과를 잃거나 오래된 세대의 결과가 섞이는 버그가 난다.
 *          그래서 규약은 여기 한 곳에 두고, 각 잡은 입력 타입과 실제 작업 본문만 가진다.
 *
 * @note 이 헤더는 구체적인 잡이나 에디터 UI 에 의존하지 않는다. 그래서 단위 테스트가 ImGui 를 끌어오지 않고 규약만
 *       따로 검증할 수 있다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

namespace sw::editor
{
    /**
     * @brief 잡 본문을 TaskManager 워커에 제출합니다. TaskManager 가 없으면(테스트 · 초기화 전) 같은 스레드에서 바로 돌립니다.
     * @details 작업 시스템은 부르는 스레드(에디터 서비스 범위를 등록한 스레드)에서 찾는다 — 워커 본문은 범위를 찾지 않는다(`findEditorScope`).
     */
    void submitEditorBackgroundJob( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args );
    // 낸 잡은 에디터 범위의 `TaskHandleTracker`(`getEditorState`)에 올라가고, `EditorContext::shutdown` 이 그것만 기다린다(엔진 전체 대기는 영구 태스크까지 기다린다).
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 입력이 없는 잡을 위한 빈 입력입니다. */
    struct EditorBackgroundNoInput
    {
    };

    /** @brief `EditorBackgroundTask::take` 가 한 번에 돌려주는 잡 상태입니다. */
    enum class EditorTaskPoll : uint8
    {
        Idle,    ///< 요청이 없거나 결과를 이미 가져갔습니다.
        Running, ///< 워커가 아직 실행 중입니다.
        Ready,   ///< 결과를 꺼냈습니다.
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorBackgroundTask
     * @brief 워커가 만들고 게임 스레드가 가져가는 잡의 공통 뼈대입니다. 락, 세대, 완료 플래그를 담당합니다.
     * @details 상태를 `shared_ptr` 로 보관해 워커에 넘긴다. 잡 객체(패널 멤버)가 먼저 사라져도 워커는 유효한 메모리에 쓰고
     *          끝난다. 세대 번호는 "요청이 갱신되었으니 오래된 워커의 결과는 버린다" 는 뜻이다.
     * @tparam TInput  워커에 넘길 입력입니다. 없으면 EditorBackgroundNoInput 입니다.
     * @tparam TResult 워커가 만들어 낼 결과입니다.
     */
    template <typename TInput, typename TResult>
    class EditorBackgroundTask
    {
    public:
        /** @brief 빈 잡을 만듭니다. */
        EditorBackgroundTask()
            : _pState{ sw::make_shared<State>() }
        {
        }

        /**
         * @brief 완료된 결과를 가져오고, 그 순간의 잡 상태를 돌려줍니다.
         * @details 결과 유무와 대기 여부를 **같은 락 안에서 한 번에** 읽는다. 따로 읽으면(`take` 가 false 인 뒤 `isPending` 을 부르면)
         *          그 사이 워커가 끝나 "대기 아님 · 결과는 아직 안 꺼냄" 이 보이고, 그 틈에 새 요청을 올리면 끝난 결과를 읽지도 못하고 버린다.
         *          그래서 "비었으면 요청" 같은 판단은 이 반환값 하나로 한다.
         *          `Ready` 이면 @p outResult 에 결과가 들어 있고 완료 플래그가 해제된다(같은 결과를 두 번 주지 않는다).
         *          `Running` 이면 워커가 아직 실행 중이고, `Idle` 이면 요청이 없거나 결과를 이미 가져갔다. 이 둘이면 @p outResult 는 그대로다.
         */
        EditorTaskPoll take( TResult& outResult )
        {
            if ( _pState == nullptr )
                return EditorTaskPoll::Idle;

            std::scoped_lock<mutex> lock{ _pState->_mutex };
            if ( _pState->_bReady == SW_FALSE )
                return _pState->_bPending == SW_TRUE ? EditorTaskPoll::Running : EditorTaskPoll::Idle;

            outResult          = std::move( _pState->_result );
            _pState->_result   = TResult{};
            _pState->_bReady   = SW_FALSE;
            _pState->_bPending = SW_FALSE;
            return EditorTaskPoll::Ready;
        }

        /**
         * @brief 워커가 아직 실행 중이면 true 입니다. 화면 표시용입니다.
         * @details 이 값으로 요청 여부를 정하지 않는다 — 결과를 꺼내는 `take` 와 따로 읽으면 어긋난다. 판단에는 `take` 의 반환값을 쓴다.
         */
        bool isPending() const
        {
            if ( _pState == nullptr )
                return false;

            std::scoped_lock<mutex> lock{ _pState->_mutex };
            return _pState->_bPending == SW_TRUE;
        }

    protected:
        /** @brief 게임 스레드와 워커가 함께 보는 상태입니다. */
        struct State
        {
            mutable mutex          _mutex{};
            TInput                 _input{};
            TResult                _result{};
            uint32                 _generation{ 0 };
            uint8                  _bPending : 1;
            uint8                  _bReady   : 1;
            [[maybe_unused]] uint8 _reserved : 6;

            State()
                : _mutex{}
                , _input{}
                , _result{}
                , _generation{ 0 }
                , _bPending{ SW_FALSE }
                , _bReady{ SW_FALSE }
                , _reserved{ 0 }
            {
            }
        };

        /**
         * @brief 입력을 싣고 세대를 올립니다.
         * @return 워커에 넘길 세대 번호입니다.
         */
        uint32 beginRequest( TInput input )
        {
            std::scoped_lock<mutex> lock{ _pState->_mutex };
            ++_pState->_generation;
            _pState->_input    = std::move( input );
            _pState->_result   = TResult{};
            _pState->_bPending = SW_TRUE;
            _pState->_bReady   = SW_FALSE;
            return _pState->_generation;
        }

        /** @brief 워커에서 호출합니다. 세대가 아직 유효하면 입력을 복사하고, 오래된 세대면 false 를 반환합니다. */
        [[nodiscard]] static bool readInput( const shared_ptr<State>& pState, uint32 generation, TInput& outInput )
        {
            if ( pState == nullptr )
                return false;

            std::scoped_lock<mutex> lock{ pState->_mutex };
            if ( generation != pState->_generation )
                return false;

            outInput = pState->_input;
            return true;
        }

        /** @brief 워커에서 호출합니다. 세대가 아직 유효하면 결과를 싣고 완료로 표시하며, 오래된 세대면 버립니다. */
        static void publish( const shared_ptr<State>& pState, uint32 generation, TResult&& result )
        {
            if ( pState == nullptr )
                return;

            std::scoped_lock<mutex> lock{ pState->_mutex };
            if ( generation != pState->_generation )
                return;

            pState->_result   = std::move( result );
            pState->_bReady   = SW_TRUE;
            pState->_bPending = SW_FALSE;
        }

        shared_ptr<State> _pState;
    };
} // namespace sw::editor
