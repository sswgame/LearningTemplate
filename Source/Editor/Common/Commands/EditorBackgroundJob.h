/**
 * @file EditorBackgroundJob.h
 * @brief 워커가 만들고 게임 스레드가 가져가는 에디터 백그라운드 잡의 공통 뼈대
 *
 * @details 에디터의 파일 스캔·로컬라이즈 로드 같은 잡은 모두 같은 규약을 쓴다 — 게임 스레드가
 *          `request()` 로 입력을 싣고 세대를 올리면, 워커가 그 세대에서 결과를 만들어 싣고,
 *          게임 스레드가 `take()` 로 가져간다. 예전에는 잠금·세대 확인·플래그 전이가 잡마다
 *          복사돼 있었는데(6벌), 한 벌만 틀려도 결과가 유실되거나 낡은 세대의 결과가 섞이는
 *          종류의 버그다. 규약을 여기 한 곳에 두고 각 잡은 **입력 타입과 실제 작업 본문**만 갖는다.
 *
 * @note 이 헤더는 구체적인 잡이나 에디터 UI 에 의존하지 않는다 — 그래서 단위 테스트가
 *       ImGui 를 끌어오지 않고 규약만 따로 검증할 수 있다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
    /** @brief 입력이 없는 잡을 위한 빈 입력. */
    struct EditorBackgroundNoInput
    {
    };

    /**
     * @class EditorBackgroundJob
     * @brief 워커가 만들고 게임 스레드가 가져가는 잡의 공통 뼈대 (잠금·세대·완료 플래그).
     * @details 상태를 `shared_ptr` 로 들고 워커에 넘긴다 — 잡 객체(패널 멤버)가 먼저 사라져도
     *          워커가 유효한 메모리에 쓰고 끝난다. 세대 번호는 "요청이 갱신됐으니 낡은 워커의
     *          결과는 버린다"는 뜻이다.
     * @tparam TInput  워커에 넘길 입력 (없으면 EditorBackgroundNoInput)
     * @tparam TResult 워커가 만들어 낼 결과
     */
    template <typename TInput, typename TResult>
    class EditorBackgroundJob
    {
    public:
        /** @brief 빈 잡을 만듭니다. */
        EditorBackgroundJob()
            : _pState{ sw::make_shared<State>() }
        {
        }

        /**
         * @brief 완료된 결과를 가져옵니다. 새 결과가 있으면 true입니다.
         * @details 한 번 가져가면 완료 표시가 내려간다 — 같은 결과를 두 번 주지 않는다.
         */
        bool take( TResult& outResult )
        {
            if ( _pState == nullptr )
                return false;

            std::scoped_lock<mutex> lock{ _pState->_mutex };
            if ( _pState->_bReady == SW_FALSE )
                return false;

            outResult          = std::move( _pState->_result );
            _pState->_result   = TResult{};
            _pState->_bReady   = SW_FALSE;
            _pState->_bPending = SW_FALSE;
            return true;
        }

        /** @brief 워커가 아직 돌고 있으면 true입니다. */
        bool isPending() const
        {
            if ( _pState == nullptr )
                return false;

            std::scoped_lock<mutex> lock{ _pState->_mutex };
            return _pState->_bPending == SW_TRUE;
        }

    protected:
        /** @brief 게임 스레드와 워커가 함께 보는 상태. */
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
         * @return 워커에 넘길 세대 번호.
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

        /** @brief 워커에서: 세대가 아직 유효하면 입력을 복사합니다. 낡은 세대면 false. */
        static bool readInput( const shared_ptr<State>& pState, uint32 generation, TInput& outInput )
        {
            if ( pState == nullptr )
                return false;

            std::scoped_lock<mutex> lock{ pState->_mutex };
            if ( generation != pState->_generation )
                return false;

            outInput = pState->_input;
            return true;
        }

        /** @brief 워커에서: 세대가 아직 유효하면 결과를 싣고 완료로 표시합니다. 낡은 세대면 버립니다. */
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
