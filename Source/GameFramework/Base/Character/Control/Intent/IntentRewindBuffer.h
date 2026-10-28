/**
 * @file IntentRewindBuffer.h
 * @brief 의도 재생의 되감기 키 상태입니다 — 일정 틱 간격으로 찍은 게임 상태 스냅샷을 메모리 상한 안에서 듭니다.
 * @details 되감기 = 목표 틱 이하의 가장 가까운 스냅샷으로 되돌리고(`GameInstanceBase::restoreSnapshot`), 그 틱부터 목표 틱까지 기록된 의도(`ControlIntentHistory`)로
 *          다시 돈다. 재생은 같은 고정 프레임이면 결정적이므로 되돌린 자리에서 다시 돈 궤적이 처음과 같다(언리얼 Replay 의 체크포인트 + 델타 재생과 같은 모양).
 *
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 되감기 키 상태 설정입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `IntentRewindBuffer::initialize` 가 오류로 거절합니다. */
    REFLECT()
    struct SW_GF_API IntentRewindConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 1, Max = 100000, Tooltip = "Ticks between two key snapshots - a seek re-simulates at most this many ticks" )
        int32 _intervalTicks{ 60 };
        PROPERTY( Min = 1, Max = 4096, Tooltip = "Memory cap of the key snapshots in megabytes (1024 x 1024 bytes); the oldest are dropped first" )
        int32 _maxMegabytes{ 64 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IntentRewindBuffer
     * @brief 틱 순서의 키 스냅샷(바이트)입니다. 상한을 넘으면 가장 오래된 것부터 버린다 — 되감을 수 있는 범위는 남은 스냅샷이 덮는 틱이다.
     * @details 되감은 뒤 거기서 다시 돌면 미래가 갈라지므로, 부르는 쪽은 되감은 틱 뒤의 스냅샷을 먼저 버린다(`discardAfter`). 게임 스레드에서만 쓴다.
     */
    class SW_GF_API IntentRewindBuffer
    {
    public:
        /** @brief 키 스냅샷 하나입니다. */
        struct KeySnapshot
        {
            vector<uint8> _bytes{};
            uint32        _tick{ 0 };
        };

        IntentRewindBuffer();

        /** @brief 설정을 받아 비운 채 시작합니다. 범위 밖이면 오류를 남기고 false 이며 이전 설정 · 내용 그대로입니다. */
        [[nodiscard]] bool initialize( const IntentRewindConfig& config );
        /** @brief 틱 @p tick 이 키를 찍을 차례(간격의 배수)이면 true 입니다. */
        bool isKeyTick( uint32 tick ) const;
        /**
         * @brief 키 스냅샷을 더합니다. 틱은 가장 새 것보다 커야 하고, 스냅샷 하나가 상한보다 크면 넣지 않습니다(둘 다 오류를 남기고 false).
         * @details 넣은 뒤 상한을 넘으면 가장 오래된 것부터 버린다.
         */
        [[nodiscard]] bool add( uint32 tick, vector<uint8> bytes );
        /** @brief @p tick 이하의 가장 가까운 키 스냅샷입니다. 없으면(모두 그 뒤이거나 비었으면) nullptr 입니다. */
        const KeySnapshot* findAtOrBefore( uint32 tick ) const;
        /** @brief 가장 오래된 키 스냅샷입니다. 비었으면 nullptr 입니다(되감을 수 있는 가장 이른 틱). */
        const KeySnapshot* findOldest() const;
        /** @brief @p tick 뒤의 키 스냅샷을 버립니다(되감은 자리에서 다시 돌기 전에). */
        void discardAfter( uint32 tick );
        /** @brief 모두 버립니다. 설정은 그대로입니다. */
        void clear();

        size_t getCount() const { return _listSnapshot.size(); }
        size_t getByteCount() const { return _byteCount; }
        uint32 getIntervalTicks() const { return _intervalTicks; }

    private:
        vector<KeySnapshot> _listSnapshot; ///< 틱 오름차순
        size_t              _byteCount;
        size_t              _maxBytes;
        uint32              _intervalTicks;
    };
} // namespace sw
