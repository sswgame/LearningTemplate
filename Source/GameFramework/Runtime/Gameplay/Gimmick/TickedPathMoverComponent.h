/**
 * @file TickedPathMoverComponent.h
 * @brief 틱 번호의 함수로 움직이는 바닥(엘리베이터 · 왕복 발판)입니다. 서버와 클라이언트가 같은 틱에 같은 자리를 냅니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
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
     *          틱은 씬의 서버 틱(`ControlSystem::getServerTick` — 조종 틱에 넷 키트가 알린 차이를 더한 값)이다. 조종 시스템이 없는 씬에서는 틱이 없으므로
     *          움직이지 않고 한 번 알린다(쌓은 시간으로 대신하지 않는다).
     *          자리는 서브틱(`kApplySubTickID`, DuringPhysics)에서 낸다. 씬에 서버 틱을 알리는 틱이 있으면(`ControlSystem::getServerTickSource`) 그것을 선행 조건으로 걸어
     *          같은 프레임에 알린 틱을 읽는다 — 예전에는 알리는 쪽이 씬의 바닥을 모두 돌며 칸마다 차이를 써 넣어(틱 안 씬 순회) 바닥 틱과 겹쳤고 프레임 차이가 순서에 달렸다.
     *          선행 조건은 알리는 쪽이 생긴 뒤 첫 서브틱이 걸고 다음 프레임부터 든다(틱 중 등록은 틱 뒤로 미뤄진다).
     *          같은 오브젝트의 Kinematic 강체는 트랜스폼을 따라가므로(`RigidBodyComponent`) 위의 캐릭터는 물리가 실어 나른다(`moveCharacter`).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Ticked Path Mover", Tooltip = "Moving floor whose position is a pure function of the network tick (same on server and clients)" )
    class SW_GF_API TickedPathMoverComponent : public Component
    {
    public:
        REFLECT_BODY();

        TickedPathMoverComponent();
        virtual ~TickedPathMoverComponent() override = default;

        /** @brief 자리를 낼 서브틱입니다. */
        static constexpr uint32 kApplySubTickID = 1;

        void onBeginPlay() override;
        /** @brief 이 씬의 서버 틱 자리로 옮깁니다(`kApplySubTickID`). 서버 틱을 알리는 틱이 바뀌었으면 선행 조건을 바꿉니다. */
        void onSubTick( uint32 subTickID, float32 deltaTime ) override;

        /**
         * @brief 시작 자리 기준 오프셋입니다. 0 초에 시작 자리(오프셋 0)에서 경유점을 차례로 `segmentSeconds` 씩 지나고, 끝에서 시작 자리로 돌아옵니다(고리).
         *        @p bPingPong 이면 끝에서 되돌아갑니다. 경유점이 없거나 구간 시간이 0 이하이면 0 입니다.
         */
        static float3 computeOffset( const vector<float3>& listWaypoint, float32 segmentSeconds, bool bPingPong, float64 seconds );
        /** @brief 경로를 정합니다 — 코드로 바닥을 세우는 쪽(테스트 · 키트)이 플레이 시작 전에 부릅니다. 데이터로 세우는 씬은 속성을 씁니다. */
        void setPath( const vector<float3>& listWaypoint, float32 segmentSeconds, bool bPingPong );
        /** @brief 틱 @p tick 의 자리로 옮깁니다. */
        void applyTick( uint32 tick );

    private:
        PROPERTY( Category = "Path", DisplayName = "Waypoints", Tooltip = "Offsets from the start position, visited in order", Units = m )
        vector<float3> _listWaypoint;
        PROPERTY( Category = "Path", DisplayName = "Segment Time", Min = 0.01, Tooltip = "Seconds between two waypoints", Units = s )
        float32 _segmentSeconds;
        PROPERTY( Category = "Path", DisplayName = "Tick Length", Min = 0.001, Tooltip = "Seconds per network tick (the fixed step)", Units = s )
        float32 _tickSeconds;
        PROPERTY( Category = "Path", DisplayName = "Ping Pong", Tooltip = "Go back along the path instead of looping to the start" )
        bool _bPingPong;

        float3        _startPosition;
        SubTickHandle _boundTickSource; ///< 지금 선행 조건으로 건 서버 틱 알림 틱(없으면 빈 핸들)
        bool          _bStarted;
        bool          _bWarnedNoTick;
    };
} // namespace sw
