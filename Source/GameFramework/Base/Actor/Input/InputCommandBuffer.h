/**
 * @file InputCommandBuffer.h
 * @brief 커맨드 입력 — 틱마다의 방향(넘패드 1..9)과 버튼을 쌓아 두고 "f,f+2" · "qcf+1" · "d/f+1+2" · "F+4"(누른 채) 같은 기술 커맨드가 방금 완성됐는지 봅니다.
 * @details 격투(철권 · 스트리트 파이터)와 액션 게임의 콤보 입력이 씁니다. 방향은 절대값(6 = 오른쪽)으로 쌓고, 맞춰 볼 때 바라보는 쪽으로 뒤집습니다(앞 = 상대 쪽).
 *          결정적입니다 — 같은 입력이면 같은 결과라 롤백 넷코드에서 다시 돌려도 됩니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 한 틱의 입력입니다. 방향은 넘패드(5 = 가운데, 6 = 오른쪽, 8 = 위). */
    struct InputFrame
    {
        uint16 _buttons{ 0 };
        uint8  _direction{ 5 };
    };
} // namespace sw

namespace sw
{
    /** @brief 커맨드의 한 단계입니다. */
    struct InputCommandStep
    {
        uint16 _buttons{ 0 };      ///< 이 단계에서 새로 눌러야 하는 버튼(모두 — 1~2 틱 차이는 동시로 본다)
        uint8  _direction{ 0 };    ///< 0 = 상관없음, 1..9 = 앞 기준 넘패드
        uint8  _bHold{ SW_FALSE }; ///< 방향을 새로 넣지 않아도 된다(누르고 있기)
    };
} // namespace sw

namespace sw
{
    /** @brief 기술 하나의 커맨드입니다. */
    struct InputCommand
    {
        hashed_string            _id{};
        vector<InputCommandStep> _listStep{};
        int32                    _maxGap{ 10 };  ///< 단계 사이 최대 틱
        int32                    _priority{ 0 }; ///< 여럿이 완성되면 높은 것(같으면 단계가 많은 것, 그것도 같으면 방향 · 버튼을 더 많이 적은 것) — "qcf+1" · "d/f+1" 이 "1" 을 이긴다
    };
} // namespace sw

namespace sw
{
    /**
     * @class InputCommandParser
     * @brief 철권 표기를 읽습니다. 쉼표로 단계를 나누고, 단계는 `방향+버튼+버튼` 입니다.
     *        방향: f b u d n, u/f d/f u/b d/b(대문자 = 누른 채), 줄임: qcf(d,d/f,f) qcb(d,d/b,b) dp(f,d,d/f). 버튼 이름은 `setButtonNames` 로(기본 "1,2,3,4").
     */
    class SW_GF_API InputCommandParser
    {
    public:
        InputCommandParser();

        void setButtonNames( string_view names );
        /** @brief 커맨드를 읽습니다. 모르는 낱말이 있으면 false 입니다. */
        [[nodiscard]] bool parse( string_view notation, InputCommand& outCommand ) const;

    private:
        [[nodiscard]] bool parseStep( string_view token, vector<InputCommandStep>& outListStep ) const;

        vector<string> _listButtonName;
    };
} // namespace sw

namespace sw
{
    /**
     * @class InputCommandBuffer
     * @brief 지난 몇십 틱의 입력입니다. 매 고정 틱 `push` 한 뒤 `findCompleted` 로 이번 틱에 끝난 커맨드를 고릅니다.
     */
    class SW_GF_API InputCommandBuffer
    {
    public:
        static constexpr int32 kSimultaneousFrames = 2; ///< 버튼 둘을 이 틱 안에 누르면 동시

        explicit InputCommandBuffer( int32 capacity = 60 );

        void push( const InputFrame& frame );
        void clear();
        /** @brief 이번 틱에 완성됐는가입니다. @p facing −1 이면 왼쪽을 본다(앞 = 4). */
        bool isCompleted( const InputCommand& command, int32 facing ) const;
        /** @brief 이번 틱에 완성된 커맨드 가운데 우선도가 가장 높은 것입니다. 없으면 nullptr 입니다. */
        const InputCommand* findCompleted( const vector<InputCommand>& listCommand, int32 facing ) const;

        /**
         * @brief 상태를 바이트로 씁니다(롤백 · 리플레이). 쌓인 프레임만 오래된 것부터 씁니다.
         * @details 버튼 16 비트 + 방향 4 비트씩입니다. 더 작게 싣고 싶은 키트는 `getFrame` 으로 직접 씁니다.
         */
        void writeState( BitWriter& outWriter ) const;
        /** @brief `writeState` 로 쓴 상태를 읽습니다. 바이트가 모자라거나 용량을 넘으면 false 이고 버퍼는 그대로입니다. */
        [[nodiscard]] bool readState( BitReader& reader );

        /** @brief 지금 쌓인 프레임 수입니다(`push` 한 수, 용량에서 멈춘다). */
        int32             getFrameCount() const { return _count; }
        int32             getCapacity() const { return _capacity; }
        const InputFrame& getFrame( int32 framesAgo ) const;
        /** @brief 바라보는 쪽에 맞춰 뒤집은 방향입니다. */
        static uint8 mirrorDirection( uint8 direction, int32 facing );

    private:
        bool matchesStep( const InputCommandStep& step, int32 framesAgo, int32 facing ) const;

        vector<InputFrame> _listFrame; ///< 고리 버퍼
        int32              _capacity;
        int32              _head; ///< 다음에 쓸 자리
        int32              _count;
    };
} // namespace sw
