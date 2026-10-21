/**
 * @file TickedPathMoverComponent.h
 * @brief 틱 번호의 함수로 움직이는 바닥(엘리베이터 · 왕복 발판)입니다. 서버와 클라이언트가 같은 틱에 같은 자리를 냅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class TickedPathMoverComponent
     * @brief 플레이 시작 자리에서 경유점(`_listWaypoint`, 시작 자리 기준 오프셋)을 차례로 지나는 바닥입니다. 자리는 **틱 번호의 순수 함수**입니다(`computeOffset`).
     * @details 결정: 움직이는 바닥 위 캐릭터의 예측이 서버와 어긋나지 않으려면, 바닥이 "지금까지 쌓은 시간" 이 아니라 "이 틱의 자리" 로 정해져야 한다.
     *          쌓은 시간은 프레임 수 · 멈춤 · 접속 시점에 따라 갈라지지만, 틱의 함수는 같은 틱이면 어디서 다시 돌려도 같다(되감아 다시 시뮬레이션해도 같다).
     *          틱은 씬의 조종 시스템 틱(`ControlSystem::getTick`, 네트워크 입력 창의 틱 번호)에 `_tickOffset` 을 더한 값이다. 클라이언트의 네트워크 키트는 서버 틱과의
     *          차이를 `setTickOffset` 으로 넣는다. 조종 시스템이 없는 씬에서는 틱이 없으므로 움직이지 않고 한 번 알린다(쌓은 시간으로 대신하지 않는다).
     *          같은 오브젝트의 Kinematic 강체는 트랜스폼을 따라가므로(`RigidBodyComponent`) 위의 캐릭터는 물리가 실어 나른다(`moveCharacter`).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Ticked Path Mover", Tooltip = "Moving floor whose position is a pure function of the network tick (same on server and clients)" )
    class SW_GF_API TickedPathMoverComponent : public Component
    {
    public:
        REFLECT_BODY();

        TickedPathMoverComponent();
        virtual ~TickedPathMoverComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /**
         * @brief 시작 자리 기준 오프셋입니다. 0 초에 시작 자리(오프셋 0)에서 경유점을 차례로 `segmentSeconds` 씩 지나고, 끝에서 시작 자리로 돌아옵니다(고리).
         *        @p bPingPong 이면 끝에서 되돌아갑니다. 경유점이 없거나 구간 시간이 0 이하이면 0 입니다.
         */
        static float3 computeOffset( const vector<float3>& listWaypoint, float32 segmentSeconds, bool bPingPong, float64 seconds );
        /** @brief 틱 @p tick 의 자리로 옮깁니다. */
        void applyTick( uint32 tick );
        /** @brief 이 씬의 틱에 더할 차이입니다(클라이언트가 서버 틱에 맞출 때). */
        void  setTickOffset( int64 tickOffset ) { _tickOffset = tickOffset; }
        int64 getTickOffset() const { return _tickOffset; }

    private:
        PROPERTY( Category = "Path", DisplayName = "Waypoints", Tooltip = "Offsets from the start position, visited in order", Units = m )
        vector<float3> _listWaypoint;
        PROPERTY( Category = "Path", DisplayName = "Segment Time", Min = 0.01, Tooltip = "Seconds between two waypoints", Units = s )
        float32 _segmentSeconds;
        PROPERTY( Category = "Path", DisplayName = "Tick Length", Min = 0.001, Tooltip = "Seconds per network tick (the fixed step)", Units = s )
        float32 _tickSeconds;
        PROPERTY( Category = "Path", DisplayName = "Ping Pong", Tooltip = "Go back along the path instead of looping to the start" )
        bool _bPingPong;

        float3 _startPosition;
        int64  _tickOffset;
        bool   _bStarted;
        bool   _bWarnedNoTick;
    };
} // namespace sw
