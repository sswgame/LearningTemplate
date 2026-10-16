/**
 * @file InputCommandBuffer.h
 * @brief 커맨드 입력을 정의합니다.
 * @details 틱마다의 방향(넘패드 1..9)과 버튼을 쌓아 두고, "f,f+2", "qcf+1", "d/f+1+2", "F+4"(누른 채) 같은 기술 커맨드가 방금 완성됐는지 본다.
 *          격투 게임(철권, 스트리트 파이터)과 액션 게임의 콤보 입력이 쓴다.
 *          방향은 절대값(6 이 오른쪽)으로 쌓고, 맞춰 볼 때 바라보는 쪽으로 뒤집는다. 앞은 상대 쪽이다.
 *          같은 입력이면 같은 결과가 나오는 결정적 계산이라 롤백 넷코드에서 다시 돌려도 된다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ValidationContext;

    /** @brief 한 틱의 입력입니다. 방향은 넘패드 표기이며 5 가 가운데, 6 이 오른쪽, 8 이 위입니다. */
    REFLECT()
    struct SW_GF_API InputFrame
    {
        REFLECT_BODY();
        PROPERTY()
        uint16 _buttons{ 0 };
        PROPERTY( Min = 0, Max = 9 )
        uint8 _direction{ 5 }; ///< 넘패드 1..9. 0 은 입력이 방향을 적지 않은 경우라 받는다
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
     * @details 방향은 f, b, u, d, n 과 u/f, d/f, u/b, d/b 를 쓰며 대문자는 누른 채라는 뜻이다.
     *          줄임 표기는 qcf(d,d/f,f), qcb(d,d/b,b), dp(f,d,d/f) 이다.
     *          버튼 이름은 `setButtonNames` 로 정하며 기본값은 "1,2,3,4" 이다.
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
     * @brief 지난 몇십 틱의 입력을 쌓아 둔 버퍼입니다. 매 고정 틱 `push` 한 뒤 `findCompleted` 로 이번 틱에 끝난 커맨드를 고릅니다.
     * @details 고리 버퍼(`_listFrame` · `_head` · `_count`)가 리플렉션 프로퍼티라서 롤백 키트는 고정 배치형(`FixedLayout`)으로 그대로 싣습니다.
     *          용량은 생성할 때 정하는 설정이라 싣지 않고, 읽은 고리 버퍼의 길이가 용량과 다르면 `validateState` 가 거절합니다.
     *          `clear` 는 칸을 중립 입력으로 되돌리므로, 쌓인 수 밖의 칸에 지난 입력이 남아 같은 버퍼가 다른 바이트가 되는 일이 없습니다.
     */
    REFLECT( Validate = validateState )
    class SW_GF_API InputCommandBuffer
    {
    public:
        REFLECT_BODY();
        static constexpr int32 kSimultaneousFrames = 2; ///< 버튼 둘을 이 틱 안에 누르면 동시

        explicit InputCommandBuffer( int32 capacity = 60 );

        void push( const InputFrame& frame );
        void clear();
        /** @brief 이번 틱에 완성됐으면 true 입니다. @p facing 이 −1 이면 왼쪽을 보며 앞이 4 입니다. */
        bool isCompleted( const InputCommand& command, int32 facing ) const;
        /** @brief 이번 틱에 완성된 커맨드 가운데 우선도가 가장 높은 것을 반환합니다. 없으면 nullptr 입니다. */
        const InputCommand* findCompleted( const vector<InputCommand>& listCommand, int32 facing ) const;

        /** @brief 지금 쌓인 프레임 수입니다. `push` 한 수이며 용량에서 멈춥니다. */
        int32             getFrameCount() const { return _count; }
        int32             getCapacity() const { return _capacity; }
        const InputFrame& getFrame( int32 framesAgo ) const;
        /** @brief 바라보는 쪽에 맞춰 뒤집은 방향입니다. */
        static uint8 mirrorDirection( uint8 direction, int32 facing );
        /** @brief 읽은 고리 버퍼가 이 버퍼의 용량과 맞는지 봅니다(`REFLECT( Validate )`). 길이, 다음 자리, 쌓인 수를 봅니다. */
        void validateState( ValidationContext& context ) const;

    private:
        bool matchesStep( const InputCommandStep& step, int32 framesAgo, int32 facing ) const;

        PROPERTY( MaxCount = 240 )
        vector<InputFrame> _listFrame; ///< 고리 버퍼. 길이는 늘 용량이고, 240 은 고정 배치형이 싣는 용량의 상한이다
        int32              _capacity;
        PROPERTY( Min = 0 )
        int32 _head; ///< 다음에 쓸 자리
        PROPERTY( Min = 0 )
        int32 _count;
    };
} // namespace sw
