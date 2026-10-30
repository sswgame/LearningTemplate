/**
 * @file ControlIntentHistory.h
 * @brief 의도 기록을 정의합니다.
 * @details 폰마다 틱별 `ControlIntent` 고리와 그 파일 형식(`.swintent`)을 다룬다. 게임플레이 리플레이, 킬캠, 버그 재현이 행동 층을 이 기록에 적는다.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Replication/TickRingBuffer.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/FileSystem/AssetID.h"

#include "GameFramework/Base/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;
    class PawnComponent;

    /** @struct ControlIntentTrack @brief 폰 하나의 기록입니다. 틱 [_firstTick, _lastTick] 이 고리에 들어 있으며, 고리가 돌면 오래된 틱부터 잊습니다. */
    struct ControlIntentTrack
    {
        TickRingBuffer<ControlIntent>   _ring{};
        hashed_string                   _pawnName{}; ///< 폰 오브젝트 이름. 재생은 이 이름으로 폰을 찾는다
        TComponentHandle<PawnComponent> _pawn{};     ///< 기록한 폰(읽은 기록이면 무효)
        uint32                          _firstTick{ 0 };
        uint32                          _lastTick{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ControlIntentHistory
     * @brief 폰마다 틱 고리 하나를 둡니다. 조종 시스템이 켜진 동안(`ControlSystem::setRecording`) 틱마다 모든 폰의 의도를 넣습니다.
     * @details 파일(`.swintent`)은 머리에 `kMagic`('SWIN', 리틀 엔디언), `kVersion`, 폰 수를 쓴다.
     *          그다음 폰마다 이름, 시작 틱(기록 시작에서 센 수), 틱 수를 쓰고, 틱마다 `ControlIntent::write` 를 쓴다.
     *          시작 상태(씬과 난수 씨앗)는 싣지 않는다. **같은 씬을 처음부터 같은 고정 프레임 시간으로** 재생할 때만 같은 궤적이 나온다.
     *          자동화 시나리오의 `fixedDelta` 와 같은 조건이다.
     *          이름이 같은 폰이 둘이면 재생은 앞의 것을 찾으므로, 기록할 폰은 이름을 다르게 둔다.
     *          언리얼의 `FSavedMove` 목록과 유니티 Netcode 의 `ICommandData` 버퍼에 해당하며, 입력 층 녹화(`InputReplay`)와 달리 키 바인딩과 무관하다.
     *          게임 스레드에서만 써야 한다.
     */
    class SW_GF_API ControlIntentHistory
    {
    public:
        static constexpr uint32      kMagic                = FourCcUtil::make( "SWIN" );
        static constexpr uint32      kVersion              = 2;
        static constexpr string_view kExtension            = ".swintent";
        static constexpr int32       kDefaultCapacityTicks = 3600; ///< 폰마다 기본 링 버퍼 크기. 60 Hz 로 1 분이다
        static constexpr int32       kMaxTrackCount        = 4096; ///< 파일 하나의 폰 수 상한(깨진 머리를 큰 할당으로 읽지 않게)
        static constexpr int32       kMaxNameBytes         = 256;  ///< 폰 이름 바이트 상한

        ControlIntentHistory();

        /** @brief 폰마다 @p capacityTicks 틱을 들 고리로 비웁니다. 1 보다 작으면 1 로 봅니다. */
        void initialize( int32 capacityTicks );
        /** @brief 기록을 모두 비웁니다. 고리 크기는 그대로입니다. */
        void reset();

        /** @brief @p pawn 의 @p tick 의도를 넣습니다. 처음 보는 폰이면 트랙을 엽니다. 틱은 폰마다 하나씩 늘어야 하며, 건너뛰면 그 틱부터 다시 시작합니다. */
        void record( uint32 tick, const TComponentHandle<PawnComponent>& pawn, const hashed_string& pawnName, const ControlIntent& intent );

        /**
         * @brief 파일 형식으로 씁니다. 틱은 가장 이른 트랙 시작을 0 으로 센다.
         * @details 고리가 돌아 앞을 잊은 트랙은 남은 틱부터 쓰며 경고한다. 그 기록은 씬 처음부터의 재생과 맞지 않는다.
         * @return 의도 하나라도 쓰지 못하면(`ControlIntent::write`) false 입니다. 그때 @p writer 에 쓴 바이트는 중간에 끊긴 것이라 쓰면 안 됩니다.
         */
        [[nodiscard]] bool write( BitWriter& writer ) const;
        /** @brief `write` 의 바이트를 읽어 이 기록을 바꿉니다. 표식, 버전, 상한이 틀리거나 바이트가 모자라면 false 와 이유를 반환하고 기록은 비워집니다. */
        [[nodiscard]] bool read( BitReader& reader, string& outError );
        /** @brief @p path 에 원자적으로 씁니다. */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /** @brief 디스크 파일 @p path(절대 경로 - 녹화 · 개발 명령)를 읽습니다. 못 읽으면 false 와 이유를 반환합니다. */
        [[nodiscard]] bool loadFromFile( string_view path, string& outError );
        /** @brief 리소스 @p assetID(씬 데이터의 트랙 칸 - 낱개 파일 또는 팩)를 읽습니다. 못 읽으면 false 와 이유를 반환합니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID, string& outError );

        int32 getTrackCount() const { return static_cast<int32>( _listTrack.size() ); }
        /** @brief 트랙 하나를 반환합니다. @p trackIndex 는 0 부터 `getTrackCount` 미만입니다. */
        const ControlIntentTrack& getTrack( int32 trackIndex ) const { return _listTrack[static_cast<size_t>( trackIndex )]; }
        /** @brief 폰 이름의 트랙 인덱스를 반환합니다. 없으면 -1 입니다. */
        int32 findTrack( const hashed_string& pawnName ) const;
        /** @brief 그 틱의 의도를 반환합니다. 범위 밖이거나 고리가 덮었으면 nullptr 입니다. */
        const ControlIntent* findIntent( int32 trackIndex, uint32 tick ) const;
        /** @brief 트랙의 남은 틱을 처음부터 순서대로 @p outListIntent 에 담습니다. 먼저 비우며, 빈 틱이 있으면 false 입니다. */
        [[nodiscard]] bool copyTrack( int32 trackIndex, vector<ControlIntent>& outListIntent ) const;
        int32              getCapacityTicks() const { return _capacityTicks; }

    private:
        /** @brief 읽은 바이트를 기록으로 옮깁니다(두 읽기 입구가 함께 쓴다). @p label 은 오류 글의 출처입니다. */
        [[nodiscard]] bool loadFromBytes( const vector<uint8>& bytes, string_view label, string& outError );

        /** @brief 고리에 남아 있는 가장 이른 틱을 반환합니다. */
        static uint32 computeOldestTick( const ControlIntentTrack& track );

        vector<ControlIntentTrack>   _listTrack;
        unordered_map<uint64, int32> _mapComponentIDToTrack; ///< 기록한 폰의 컴포넌트 id 에서 트랙 인덱스로 가는 맵
        int32                        _capacityTicks;
    };
} // namespace sw
