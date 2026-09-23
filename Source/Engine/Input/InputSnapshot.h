/**
 * @file InputSnapshot.h
 * @brief 롤백 넷코드와 리플레이 재생을 위한 프레임 틱 입력 스냅샷과 링 버퍼입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"
#include "Core/Math/Math.h"

namespace sw
{
    /**
     * @struct InputSnapshot
     * @brief 한 프레임(틱)의 버튼 비트마스크, 2D 벡터, 아날로그 트리거 압력 스냅샷입니다.
     */
    struct SW_API InputSnapshot
    {
        uint32  _tickNumber{ 0 };
        uint64  _buttonMask{ 0 };          ///< 최대 64개 액션 · 버튼 눌림 비트마스크. 불리언이 아니라 마스크.
        float2  _moveVector{ 0.0f, 0.0f }; ///< 이동 2D 벡터
        float2  _lookVector{ 0.0f, 0.0f }; ///< 시점 2D 벡터
        float32 _leftTrigger{ 0.0f };      ///< LT 아날로그 압력 (0.0 ~ 1.0)
        float32 _rightTrigger{ 0.0f };     ///< RT 아날로그 압력 (0.0 ~ 1.0)

        /**
         * @brief 직렬화한 스냅샷 한 개의 바이트 수입니다. **`sizeof(InputSnapshot)` 과 다릅니다.**
         * @details 구조체에는 `_tickNumber` 뒤에 정렬 패딩 4바이트가 있습니다. 예전에는 구조체를
         *          통째로 `memcpy` 했기 때문에 **그 패딩까지 파일 · 네트워크로 나갔습니다.** 패딩은
         *          아무도 쓰지 않으므로 값이 정해져 있지 않고, 그래서 같은 입력을 두 번 저장해도
         *          바이트가 달라질 수 있었습니다(체크섬 · 비교 · 중복 제거가 성립하지 않습니다).
         *          지금은 필드를 순서대로 적습니다. 컴파일러 · 아키텍처가 달라도 같은 바이트가 나옵니다.
         */
        static constexpr uint32 kSerializedSize = sizeof( uint32 ) + sizeof( uint64 ) + ( 4 * sizeof( float32 ) ) + ( 2 * sizeof( float32 ) );

        /** @brief 바이너리 버퍼에 직렬화합니다(성공하면 적은 바이트 수 = `kSerializedSize`). */
        uint32 serialize( uint8* pOutBuffer, uint32 bufferSize ) const;

        /** @brief 바이너리 버퍼에서 역직렬화합니다. */
        bool deserialize( const uint8* pBuffer, uint32 bufferSize );
    };

    /**
     * @class InputHistoryBuffer
     * @brief 최근 N개 틱의 입력을 보관하는 롤백 · 리플레이 전용 순환 링 버퍼입니다.
     */
    class SW_API InputHistoryBuffer
    {
    public:
        static constexpr size_t kDefaultCapacity = 256; ///< 약 4초 분량(60Hz 기준)
        // 링 인덱스를 `& (kDefaultCapacity - 1)` 로 감는다 — 2의 거듭제곱이 아니면 조용히 어긋난다.
        static_assert( ( kDefaultCapacity & ( kDefaultCapacity - 1 ) ) == 0, "kDefaultCapacity 는 2의 거듭제곱이어야 합니다." );

        InputHistoryBuffer();

        /** @brief 현재 틱 스냅샷을 링 버퍼에 기록합니다. */
        void recordSnapshot( const InputSnapshot& snapshot );

        /** @brief 특정 틱 번호의 스냅샷을 조회합니다(범위 밖이면 nullptr). */
        const InputSnapshot* getSnapshot( uint32 tickNumber ) const;

        /** @brief 가장 최근에 기록된 스냅샷을 반환합니다. */
        const InputSnapshot* getLatestSnapshot() const;

        /** @brief 링 버퍼를 비웁니다. */
        void clear();

        /** @brief 현재 기록된 스냅샷 개수를 반환합니다. */
        size_t getCount() const { return _count; }

    private:
        array<InputSnapshot, kDefaultCapacity> _arrHistory;
        size_t                                 _writeIndex;
        size_t                                 _count;
        uint32                                 _latestTick;
    };
} // namespace sw
