/**
 * @file KartGhost.h
 * @brief 타임어택 고스트 — 고정 걸음마다 입력을 양자화해 기록하고, 같은 차 · 같은 트랙에서 같은 규칙으로 다시 돌려 같은 경로를 냅니다.
 * @details 위치를 기록하지 않고 **입력만** 기록합니다(한 걸음 3 바이트). 다시 돌려도 같은 경로인 까닭은 세 가지입니다 — 차(`ArcadeVehicleMotor`)는 고정 걸음이고
 *          결정적이며, 기록하는 쪽(`KartRace`)도 그 차에 **양자화한 입력**을 넣고(`quantizeInput`), 차에 손대는 트랙 규칙(부스트 패드)을 같은 순서로
 *          `KartGhostPlayer` 가 되풀이합니다. 아이템 · 다른 차와 부딪힘은 고스트에 없습니다 — 타임어택은 혼자 달린다.
 *          같은 프레임 형식(`writeFrame` · `readFrame`)은 락스텝 넷 게임이 입력을 실어 보낼 때도 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Actor/Movement/ArcadeVehicleMotor.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;
    class KartTrack;

    /** @brief 한 걸음의 입력(양자화)입니다. */
    struct KartGhostFrame
    {
        int8  _throttle{ 0 }; ///< −127..127 → −1..1
        int8  _steer{ 0 };
        uint8 _buttons{ 0 }; ///< `KartGhost::kButton*`
    };
} // namespace sw

namespace sw
{
    /**
     * @class KartGhost
     * @brief 한 번의 주행 기록 — 출발 자리 · 방향 · 걸음 크기와 걸음마다 입력입니다.
     */
    class SW_GF_API KartGhost
    {
    public:
        static constexpr uint8 kButtonDrift   = 1u << 0;
        static constexpr uint8 kButtonBoost   = 1u << 1;
        static constexpr uint8 kButtonJump    = 1u << 2;
        static constexpr uint8 kButtonUseItem = 1u << 3;
        static constexpr uint8 kVersion       = 1;

        KartGhost();

        static KartGhostFrame     encodeInput( const ArcadeVehicleInput& input, bool bUseItem = false );
        static ArcadeVehicleInput decodeInput( const KartGhostFrame& frame );
        /** @brief 기록했다가 되돌린 것과 똑같은 입력입니다 — 기록하는 차는 이것으로 달린다. */
        static ArcadeVehicleInput quantizeInput( const ArcadeVehicleInput& input ) { return decodeInput( encodeInput( input ) ); }
        static void               writeFrame( BitWriter& writer, const KartGhostFrame& frame );
        static KartGhostFrame     readFrame( BitReader& reader );

        /** @brief 기록을 비우고 출발 자리 · 방향(라디안) · 걸음 크기를 둡니다. */
        void begin( const float3& startPosition, float32 startYaw, float32 step );
        void record( const ArcadeVehicleInput& input, bool bUseItem = false );
        void setFinishTime( float32 finishTime ) { _finishTime = finishTime; }

        /** @brief 바이트로 씁니다(세이브 · 서버 순위표). */
        void serialize( vector<uint8>& outBuffer ) const;
        /** @brief 바이트를 읽습니다. 형식이 다르거나 잘렸으면 false 이고 그대로입니다. */
        [[nodiscard]] bool deserialize( const uint8* pData, int32 byteCount );

        const vector<KartGhostFrame>& getFrames() const { return _listFrame; }
        int32                         getFrameCount() const { return static_cast<int32>( _listFrame.size() ); }
        const float3&                 getStartPosition() const { return _startPosition; }
        float32                       getStartYaw() const { return _startYaw; }
        float32                       getStep() const { return _step; }
        float32                       getFinishTime() const { return _finishTime; }

    private:
        vector<KartGhostFrame> _listFrame;
        float3                 _startPosition;
        float32                _startYaw;
        float32                _step;
        float32                _finishTime; ///< 0 = 끝까지 달리지 않은 기록
    };
} // namespace sw

namespace sw
{
    /**
     * @class KartGhostPlayer
     * @brief 고스트를 자기 차로 다시 돌립니다. 트랙(빌려 쓴다)이 있으면 땅 · 부스트 패드를 기록할 때와 같이 씁니다.
     */
    class SW_GF_API KartGhostPlayer
    {
    public:
        KartGhostPlayer();

        /** @brief @p ghost 를(빌려 쓴다) 처음부터 돌릴 준비를 합니다. */
        void initialize( const KartGhost* pGhost, const ArcadeVehicleSettings& settings, const KartTrack* pTrack );
        /** @brief 한 걸음 나아갑니다. 기록이 끝났으면 false 입니다. */
        bool step();
        /** @brief 프레임 시간만큼(고정 걸음으로 나눠) 나아갑니다. 걸은 수입니다. */
        int32 advance( float32 frameTime );

        bool                      isFinished() const;
        int32                     getFrameIndex() const { return _frameIndex; }
        const ArcadeVehicleMotor& getMotor() const { return _motor; }

    private:
        ArcadeVehicleMotor _motor;
        FixedStepTimer     _timer;
        const KartGhost*   _pGhost;
        const KartTrack*   _pTrack;
        int32              _frameIndex;
        int32              _lastBoostPad;
    };
} // namespace sw
