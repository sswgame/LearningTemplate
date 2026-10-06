/**
 * @file AdventureGimmicks.h
 * @brief 어드벤처 기믹 — 미는 블록(칸 단위), 오브젝트 원소 상태(횃불 퍼즐 · 타는 상자)입니다. 스위치 · 열쇠 문은 상호작용 + 회로 프리팹입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Gimmick/ElementGrid.h"
#include "GameFramework/Base/Utility/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class PushBlockComponent
     * @brief 칸 단위로 미는 블록(젤다 · 소코반) — `push( 방향 )` 은 가장 가까운 축으로 맞춘 한 칸 앞이 비었으면(`WorldQuery` 광선) `_moveTime` 동안 옮깁니다.
     * @details 같은 오브젝트의 `InteractableComponent`(Push)를 끝내면 끝낸 이에게서 블록 쪽으로 밉니다(센서의 사용 알림을 읽는다). 2D 는 `_bPlanar2D`(XY 축).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Push Block", Tooltip = "Grid-snapped block pushed one cell at a time if the cell ahead is free" )
    class SW_GF_API PushBlockComponent : public Component
    {
    public:
        REFLECT_BODY();

        PushBlockComponent();
        virtual ~PushBlockComponent() override = default;

        void onTick( float32 deltaTime ) override;
        /** @brief 한 걸음(60 Hz)입니다. */
        void stepOnce();
        /** @brief @p direction 의 가장 가까운 축으로 한 칸 밉니다. 움직이는 중이거나 막혔으면 false 입니다. */
        [[nodiscard]] bool push( const float3& direction );
        bool               isMoving() const { return _stepsLeft > 0; }

    private:
        PROPERTY( Category = "Push", DisplayName = "Cell Size", Min = 0.01, Units = m )
        float32 _cellSize;
        PROPERTY( Category = "Push", DisplayName = "Move Time", Min = 0.0, Units = s )
        float32 _moveTime;
        PROPERTY( Category = "Push", DisplayName = "Planar 2D", Tooltip = "Push along X/Y instead of X/Z" )
        bool _bPlanar2D;
        PROPERTY( Category = "Push", DisplayName = "From", Tooltip = "Runtime: cell the move started from", Units = m )
        float3 _from;
        PROPERTY( Category = "Push", DisplayName = "To", Tooltip = "Runtime: cell the move ends at", Units = m )
        float3 _to;
        PROPERTY( Category = "Push", DisplayName = "Steps Left", Tooltip = "Runtime" )
        int32 _stepsLeft;
        PROPERTY( Category = "Push", DisplayName = "Move Steps", Tooltip = "Runtime" )
        int32 _moveSteps;

        FixedStepTimer _clock;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ElementStatusComponent
     * @brief 오브젝트 하나의 원소 상태 — 공용 원소 규칙표의 1 × 1 격자입니다(횃불 · 타는 나무 상자 · 얼음 덩어리). `applyStimulus( "Fire" )` 처럼 이름으로 댑니다.
     * @details 같은 오브젝트에 `GimmickSensorComponent` 가 있으면 `_signalStatus` 가 있는 동안 신호를 1 로 둡니다 — 횃불 넷을 Signal 센서 → And → 문으로
     *          이으면 횃불 퍼즐입니다. 걸음은 표의 걸음 시간입니다(결정적).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Element Status", Tooltip = "One object's element state (torch, burning crate) from the shared element rule table" )
    class SW_GF_API ElementStatusComponent : public Component
    {
    public:
        REFLECT_BODY();

        ElementStatusComponent();
        virtual ~ElementStatusComponent() override = default;

        void onPostLoad() override;
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 자극을 이름으로 댑니다(Fire · Water · Ice · Electric). 바뀌었으면 true 입니다. 모르는 이름 · 표가 없으면 false 입니다. */
        [[nodiscard]] bool applyStimulus( const hashed_string& stimulus );
        /** @brief 상태가 있는가입니다(Burning · Charged). */
        bool hasStatus( const hashed_string& status ) const;
        /** @brief 재질 이름을 바꿉니다(상태는 지운다). */
        void setMaterial( const hashed_string& material );

    private:
        void rebuild();
        void publishSignal();

    private:
        PROPERTY( Category = "Element", DisplayName = "Rule Table", AssetPath, Tooltip = "Element rule table; empty uses the common default" )
        string _tablePath;
        PROPERTY( Category = "Element", DisplayName = "Material", Tooltip = "Material name in the table" )
        hashed_string _material;
        PROPERTY( Category = "Element", DisplayName = "Signal Status", Tooltip = "Status that sets the gimmick sensor signal (Burning for torches)" )
        hashed_string _signalStatus;
        PROPERTY( Category = "Element", DisplayName = "Start Status", Tooltip = "Stimulus applied at play start (Fire for a lit torch)" )
        hashed_string _startStimulus;

        ElementGrid _grid;
        uint32      _seenTableReloadCount; ///< 표를 찾을 때의 `ElementRuleTable::getSharedReloadCount` — 달라지면 다시 짓는다
    };
} // namespace sw
