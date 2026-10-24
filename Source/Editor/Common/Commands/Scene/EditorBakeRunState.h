/**
 * @file EditorBakeRunState.h
 * @brief 에디터 프레임에 나눠 도는 베이크 세션(라이트 프로브 · 반사 프로브)이 함께 쓰는 실행 상태입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /**
     * @class EditorBakeRunState
     * @brief 프레임마다 조금씩 도는 베이크 하나의 "돌고 있는가 · 계속해도 되는가" 를 맡습니다. 베이크 본문(무엇을 어떻게 굽는가)은 세션이 가집니다.
     * @details 베이크는 시작할 때의 씬을 읽으며 여러 프레임에 걸쳐 돈다. 그래서 시작하면 씬 편집을 막고(`EditorPlaySession::holdSceneEdits`), 프레임마다 씬이 바뀌었는지(세대),
     *          플레이가 시작됐는지, 잠금을 지나친 편집이 있었는지 묻는다(`findStopReason`). 하나라도 해당하면 세션은 멈춘다. 끝난(취소 포함) 횟수는 씬 세대마다 센다.
     */
    class EditorBakeRunState
    {
    public:
        /// @brief 한 프레임에 베이크에 쓰는 시간(ms)입니다. 프레임 하나(16 ms)의 절반이라 에디터가 30 fps 밑으로 떨어지지 않는다.
        static constexpr int64 kFrameBudgetMilliseconds = 8;

        EditorBakeRunState();
        /** @brief 돌던 중에 사라지면 편집 잠금을 풉니다. */
        ~EditorBakeRunState();

        EditorBakeRunState( const EditorBakeRunState& )            = delete;
        EditorBakeRunState& operator=( const EditorBakeRunState& ) = delete;

        /** @brief 씬 세대 @p sceneGeneration 의 베이크를 시작합니다. 편집 잠금을 겁니다. 돌고 있지 않을 때만 부릅니다. */
        void begin( uint64 sceneGeneration );
        /** @brief 돌던 베이크를 끝냅니다(취소 포함). 편집 잠금을 풀고 끝난 횟수를 셉니다. 돌고 있을 때만 부릅니다. */
        void end();
        /**
         * @brief 계속하면 안 되는 이유입니다(정적 문자열). 계속해도 되면 nullptr 입니다.
         * @details 활성 씬이 없거나 세대가 바뀌었다 / 플레이가 시작됐다 / 잠금을 지나친 편집이 있었다.
         */
        const utf8* findStopReason() const;

        bool isRunning() const { return _bRunning; }
        /**
         * @brief 씬 세대 @p sceneGeneration 에서 끝난(취소 포함) 베이크 횟수입니다. 다른 세대의 베이크는 세지 않는다.
         * @details 세션은 에디터와 함께 살아 시나리오 체인(`-scenario=a,b`)을 넘어 남는다 — 앞 시나리오의 베이크가 뒤 시나리오의 수에 섞이지 않게 씬마다 센다.
         */
        uint32 getFinishedRunCount( uint64 sceneGeneration ) const { return sceneGeneration == _finishedRunSceneGeneration ? _finishedRunCount : 0u; }

    private:
        uint64 _sceneGeneration;            ///< 시작할 때의 씬 세대 — 바뀌면 멈춘다
        uint64 _finishedRunSceneGeneration; ///< `_finishedRunCount` 를 센 씬 세대
        uint32 _heldEditCount;              ///< 시작할 때의 `EditorPlaySession::getHeldSceneEditCount` — 바뀌면(잠금을 지나친 편집) 멈춘다
        uint32 _finishedRunCount;
        bool   _bRunning;
    };
} // namespace sw::editor
