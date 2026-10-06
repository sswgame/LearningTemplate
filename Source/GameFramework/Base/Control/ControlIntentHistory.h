/**
 * @file ControlIntentHistory.h
 * @brief 의도 기록 — 폰마다 틱별 `ControlIntent` 고리와 그 파일 형식(`.swintent`)입니다. 게임플레이 리플레이 · 킬캠 · 버그 재현이 행동 층을 적습니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/Replication/TickRingBuffer.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @struct ControlIntentTrack @brief 폰 하나의 기록입니다. 틱 [_firstTick, _lastTick] 이 고리에 듭니다(고리가 돌면 오래된 틱부터 잊는다). */
    struct ControlIntentTrack
    {
        TickRingBuffer<ControlIntent> _ring{};
        hashed_string                 _pawnName{}; ///< 폰 오브젝트 이름 — 재생은 이 이름으로 폰을 찾는다
        ComponentHandle               _pawn{};     ///< 기록한 폰(읽은 기록이면 무효)
        uint32                        _firstTick{ 0 };
        uint32                        _lastTick{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ControlIntentHistory
     * @brief 폰마다 틱 고리 하나 — 조종 시스템이 켜진 동안(`ControlSystem::setRecording`) 틱마다 모든 폰의 의도를 넣습니다.
     * @details 파일(`.swintent`) = 머리 `kMagic`('SWIN', 리틀 엔디언) · `kVersion` · 폰 수, 폰마다 이름 · 시작 틱(기록 시작에서 센 수) · 틱 수 · 틱마다
     *          `ControlIntent::write`. 시작 상태(씬 · 난수 씨앗)는 싣지 않는다 — **같은 씬을 처음부터 같은 고정 프레임 시간으로** 재생할 때만 같은
     *          궤적이다(자동화 시나리오의 `fixedDelta` 와 같은 조건). 이름이 같은 폰이 둘이면 재생은 앞의 것을 찾는다 — 기록할 폰은 이름을 다르게 둔다.
     *          언리얼 `FSavedMove` 목록 · 유니티 Netcode `ICommandData` 버퍼의 자리이고, 입력 층 녹화(`InputReplay`)와 달리 키 바인딩과 무관하다.
     *          게임 스레드에서만 씁니다.
     */
    class SW_GF_API ControlIntentHistory
    {
    public:
        static constexpr uint32      kMagic                = FourCcUtil::make( "SWIN" );
        static constexpr uint32      kVersion              = 1;
        static constexpr string_view kExtension            = ".swintent";
        static constexpr int32       kDefaultCapacityTicks = 3600; ///< 폰마다 기본 고리 — 60 Hz 로 1 분
        static constexpr int32       kMaxTrackCount        = 4096; ///< 파일 하나의 폰 수 상한(깨진 머리를 큰 할당으로 읽지 않게)
        static constexpr int32       kMaxNameBytes         = 256;  ///< 폰 이름 바이트 상한

        ControlIntentHistory();

        /** @brief 폰마다 @p capacityTicks 틱을 들 고리로 비웁니다(1 보다 작으면 1). */
        void initialize( int32 capacityTicks );
        /** @brief 기록을 모두 비웁니다(고리 크기는 그대로). */
        void reset();

        /** @brief @p pawn 의 @p tick 의도를 넣습니다. 처음 보는 폰이면 트랙을 엽니다. 틱은 폰마다 하나씩 늘어야 한다(건너뛰면 그 틱부터 다시 시작). */
        void record( uint32 tick, const ComponentHandle& pawn, const hashed_string& pawnName, const ControlIntent& intent );

        /**
         * @brief 파일 형식으로 씁니다. 틱은 가장 이른 트랙 시작을 0 으로 센다.
         * @details 고리가 돌아 앞을 잊은 트랙은 남은 틱부터 쓴다(경고) — 그 기록은 씬 처음부터의 재생과 맞지 않는다.
         */
        void write( BitWriter& writer ) const;
        /** @brief `write` 의 바이트를 읽어 이 기록을 바꿉니다. 표식 · 판 · 상한이 틀리거나 바이트가 모자라면 false 와 이유이고 기록은 빈다. */
        [[nodiscard]] bool read( BitReader& reader, string& outError );
        /** @brief @p path 에 씁니다(원자적). */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief @p path 를 읽습니다. 못 읽으면 false 와 이유입니다. */
        [[nodiscard]] bool loadFromFile( string_view path, string& outError );

        int32 getTrackCount() const { return static_cast<int32>( _listTrack.size() ); }
        /** @brief 트랙 하나입니다(@p trackIndex 는 0..`getTrackCount`). */
        const ControlIntentTrack& getTrack( int32 trackIndex ) const { return _listTrack[static_cast<size_t>( trackIndex )]; }
        /** @brief 폰 이름의 트랙 자리입니다. 없으면 −1 입니다. */
        int32 findTrack( const hashed_string& pawnName ) const;
        /** @brief 그 틱의 의도입니다. 없으면(범위 밖 · 고리가 덮음) nullptr 입니다. */
        const ControlIntent* findIntent( int32 trackIndex, uint32 tick ) const;
        /** @brief 트랙의 남은 틱을 처음부터 순서대로 @p outListIntent 에 담습니다(먼저 비운다). 빈 틱이 있으면 false 입니다. */
        [[nodiscard]] bool copyTrack( int32 trackIndex, vector<ControlIntent>& outListIntent ) const;
        int32              getCapacityTicks() const { return _capacityTicks; }

    private:
        /** @brief 고리에 남아 있는 가장 이른 틱입니다. */
        static uint32 computeOldestTick( const ControlIntentTrack& track );

        vector<ControlIntentTrack>   _listTrack;
        unordered_map<uint64, int32> _mapComponentIdToTrack; ///< 기록한 폰의 컴포넌트 id → 트랙 자리
        int32                        _capacityTicks;
    };
} // namespace sw
