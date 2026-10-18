/**
 * @file AdventureGimmicks.h
 * @brief 어드벤처 기믹입니다. 미는 블록(격자 한 칸 단위)과 오브젝트의 원소 상태(횃불 퍼즐, 타는 상자)를 다룹니다. 스위치와 열쇠 문은 상호작용과 회로 프리팹으로 만듭니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/Base/Gameplay/Gimmick/ElementGrid.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class PushBlockComponent
     * @brief 한 칸 단위로 미는 블록입니다(젤다, 소코반). `push( 방향 )` 은 방향을 가장 가까운 축으로 맞추고, 한 칸 앞이 비어 있으면(`WorldQuery` 광선으로 확인) `_moveTime` 동안 블록을 옮깁니다.
     * @details 같은 오브젝트의 `InteractableComponent`(Push)를 끝내면 끝낸 쪽에서 블록 방향으로 민다. 센서의 사용 알림을 읽어서 처리한다. 2D 는 `_bPlanar2D`(XY 축)를 켠다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Push Block", Tooltip = "Grid-snapped block pushed one cell at a time if the cell ahead is free" )
    class SW_GF_API PushBlockComponent : public Component
    {
    public:
        REFLECT_BODY();

        PushBlockComponent();
        virtual ~PushBlockComponent() override = default;

        void onTick( float32 deltaTime ) override;
        /** @brief 한 스텝(60 Hz)입니다. */
        void stepOnce();
        /** @brief @p direction 에서 가장 가까운 축으로 한 칸 밉니다. 움직이는 중이거나 막혀 있으면 false 입니다. */
        [[nodiscard]] bool push( const float3& direction );
        bool               isMoving() const { return _stepsLeft > 0; }

    private:
        PROPERTY( Category = "Push", DisplayName = "Cell Size", Min = 0.01, Units = m )
        float32 _cellSize;
        PROPERTY( Category = "Push", DisplayName = "Move Time", Min = 0.0, Units = s )
        float32 _moveTime;
        PROPERTY( Category = "Push", DisplayName = "Planar 2D", Tooltip = "Push along X/Y instead of X/Z" )
        bool _bPlanar2D;
        PROPERTY( RuntimeState, Category = "Push", DisplayName = "From", Tooltip = "Runtime: cell the move started from", Units = m )
        float3 _from;
        PROPERTY( RuntimeState, Category = "Push", DisplayName = "To", Tooltip = "Runtime: cell the move ends at", Units = m )
        float3 _to;
        PROPERTY( RuntimeState, Category = "Push", DisplayName = "Steps Left", Tooltip = "Runtime" )
        int32 _stepsLeft;
        PROPERTY( RuntimeState, Category = "Push", DisplayName = "Move Steps", Tooltip = "Runtime" )
        int32 _moveSteps;

        FixedStepTimer _clock;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ElementStatusComponent
     * @brief 오브젝트 하나의 원소 상태입니다. 공용 원소 규칙 테이블을 따르는 1 × 1 격자이며 횃불, 타는 나무 상자, 얼음 덩어리에 씁니다. `applyStimulus( "Fire" )` 처럼 이름으로 자극을 가합니다.
     * @details 같은 오브젝트에 `GimmickSensorComponent` 가 있으면 `_signalStatus` 상태가 있는 동안 신호를 1 로 둔다. 횃불 네 개를 Signal 센서, And, 문 순서로
     *          연결하면 횃불 퍼즐이 된다. 스텝 간격은 테이블의 스텝 시간이며 결정적이다.
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

        /** @brief 자극을 이름으로 가합니다(Fire, Water, Ice, Electric). 바뀌었으면 true 입니다. 알 수 없는 이름이거나 테이블이 없으면 false 입니다. */
        [[nodiscard]] bool applyStimulus( const hashed_string& stimulus );
        /** @brief 상태가 있으면 true 입니다(Burning, Charged). */
        bool hasStatus( const hashed_string& status ) const;
        /** @brief 재질 이름을 바꿉니다. 상태는 지웁니다. */
        void setMaterial( const hashed_string& material );
        /** @brief 원소 규칙 테이블 경로를 바꾸고 다시 만듭니다. 비우면 테이블이 없어 자극이 듣지 않는다(오류로 알린다). */
        void setTablePath( string_view path );

    private:
        void rebuild();
        void publishSignal();

    private:
        PROPERTY( Category = "Element", DisplayName = "Rule Table", AssetPath, Tooltip = "Element rule table (required)" )
        string _tablePath;
        PROPERTY( Category = "Element", DisplayName = "Material", Tooltip = "Material name in the table" )
        hashed_string _material;
        PROPERTY( Category = "Element", DisplayName = "Signal Status", Tooltip = "Status that sets the gimmick sensor signal (Burning for torches)" )
        hashed_string _signalStatus;
        PROPERTY( Category = "Element", DisplayName = "Start Status", Tooltip = "Stimulus applied at play start (Fire for a lit torch)" )
        hashed_string _startStimulus;

        ElementGrid _grid;
        uint32      _seenTableReloadCount; ///< 테이블을 찾았을 때의 `ElementRuleTable::getSharedReloadCount` 값. 달라지면 다시 만든다
    };
} // namespace sw
