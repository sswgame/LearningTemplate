/**
 * @file InputActionBuffer.h
 * @brief 선입력 버퍼와 격투 게임식 커맨드 판정(최근 트리거된 액션 이력)을 담습니다. `InputMap` 이 한 개를 들고 프레임마다 진행합니다.
 * @details - `bufferAction` · `consumeBufferedAction`: 공격 버튼을 살짝 일찍 눌러도 인정해 주는 선입력 유예 링 버퍼입니다.
 *          - `wasCommandSequenceTriggered`: 최근 트리거된 액션 이력에서 주어진 순서가 시간 창 안에 나왔는지 검사합니다.
 *          - `wasCommandPatternTriggered`: "236P"(하, 우하, 우 + 펀치) 같은 넘패드 표기를 액션 이름 시퀀스로 바꿔 위 판정에 맡깁니다.
 *          매핑(바인딩 · 레이어 · 평가)과 갈라 둔 까닭은 상태와 수명이 다르기 때문입니다. 매핑은 데이터에서 오고, 이 버퍼는 게임플레이가 프레임마다 쓰고 비웁니다.
 *          언리얼은 Enhanced Input 이 매핑을 맡고 콤보 · 선입력은 게임플레이 쪽(GAS 의 입력 버퍼 등)이 맡는 것과 같은 나눔입니다.
 *          틱 단위로 결정적인 격투 커맨드(롤백 넷코드, 바라보는 쪽 뒤집기)는 GameFramework 의 `InputCommandBuffer` 가 맡습니다. 이 버퍼는 실시간 액션 이력입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/RingBuffer.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @class InputActionBuffer
     * @brief 선입력 링 버퍼와 커맨드 이력 링 버퍼입니다.
     */
    class SW_API InputActionBuffer
    {
    public:
        InputActionBuffer();

        void clear();
        /** @brief 시간을 진행하고, 만료된 선입력과 이력 보관 시간(`kHistorySeconds`)이 지난 커맨드를 지웁니다. */
        void update( float32 deltaSeconds );
        /** @brief 이번 프레임에 트리거된 액션을 이력에 남깁니다(`InputMap::update` 가 부른다). */
        void recordTriggered( const hashed_string& action );

        void bufferAction( const hashed_string& action, float32 expirationSeconds = 0.2f );
        bool consumeBufferedAction( const hashed_string& action );

        /** @brief 지정한 시간 창 안에 커맨드 시퀀스(예: ["Down", "DownRight", "Right", "Attack"])가 이어서 입력됐는지 검사합니다. */
        bool wasCommandSequenceTriggered( const vector<hashed_string>& listSequence, float32 maxWindowSeconds = 0.35f ) const;
        bool wasCommandPatternTriggered( const hashed_string& pattern, float32 maxWindowSeconds = 0.8f ) const;

    private:
        static constexpr uint32  kMaxBufferedActions = 16;
        static constexpr uint32  kMaxCommandHistory  = 32;
        static constexpr float32 kHistorySeconds     = 2.0f; ///< 이보다 오래된 커맨드 이력은 판정에 쓰지 않는다

        struct BufferedActionItem
        {
            hashed_string _action{};
            float32       _remainingTime{ 0.0f };
        };

        struct CommandHistoryEntry
        {
            hashed_string _action{};
            float32       _timestamp{ 0.0f };
        };

        RingBuffer<BufferedActionItem, kMaxBufferedActions> _listBufferedAction; ///< 가득 차면 가장 오래된 선입력을 덮는다
        RingBuffer<CommandHistoryEntry, kMaxCommandHistory> _listCommandHistory; ///< 가득 차면 가장 오래된 이력을 덮는다
        float32                                             _elapsedSeconds;
    };
} // namespace sw
