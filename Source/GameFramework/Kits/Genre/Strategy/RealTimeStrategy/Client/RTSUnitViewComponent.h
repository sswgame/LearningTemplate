/**
 * @file RTSUnitViewComponent.h
 * @brief RTS 키트의 클라이언트 쪽 유닛 하나의 모습 — 디렉터 월드의 같은 id 유닛을 따라 자리 · 크기(지은 만큼 · 남은 자원만큼) · 방향 · 편 색 · 고름을 맞춥니다.
 */
#pragma once
#include "Core/Container/SlotHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameFrameworkComponent.h"

namespace sw
{
    /**
     * @class RTSUnitViewComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터 · 전장 뷰(색) · 명령 입력(고름)을 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 그 오브젝트는 핸들로 들고 매 프레임 풉니다.
     * @details 모델은 전장 뷰(`RTSBattlefieldViewComponent`)가 꺼낼 때 겁니다(유닛 종류마다 규칙 → 뷰 카탈로그 `units.views.xml` 이 정한다 — 감사 K-7). 움직이는 유닛은 움직인 쪽을 보고(키트 모델의 앞이 +Z) 멈추면 마지막 방향을 지킵니다.
     *          유닛이 죽거나 안개에 가려지면 전장 뷰가 풀로 돌려줍니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "RTS Unit View", Tooltip = "Follows one unit of the RTS game mode world" )
    class SW_KIT_CLIENT_API RTSUnitViewComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( RTSUnitViewComponent );

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 디렉터(PrePhysics)가 이 프레임의 판을 끝낸 뒤에 읽는다.

        RTSUnitViewComponent();
        virtual ~RTSUnitViewComponent() override = default;

        void onTick( TickContext& context ) override;

        /** @brief 풀 반환: 자리를 맞추기 전(숨은 채)으로 돌리고 따라가던 유닛을 잊습니다. 모델은 전장 뷰가 꺼낸 뒤 다시 겁니다. */
        [[nodiscard]] SpawnStateReset resetToSpawnState() override;
        /** @brief 따라갈 디렉터 · 유닛 · 모델 폭을 정합니다(전장 뷰가 꺼낸 뒤 부른다). */
        void assignUnit( GameObjectHandle gameMode, SlotHandle unitID, float32 modelWidth );

        /**
         * @brief 이 뷰의 메시 자리와 디렉터가 지금 가진 유닛 자리의 수평 거리입니다(감시 값 `Skirmish.ViewLagMax` 가 쓴다). 비교할 수 없으면(유닛이 없거나 아직 자리를 맞추기 전) -1 입니다.
         * @details 뷰가 디렉터보다 뒤 그룹에서 돌면 한 프레임이 끝났을 때 0 이다. 앞 그룹에서 돌면 유닛이 움직인 만큼 뒤처진다 — 그룹 순서를 지키는지 시나리오가 본다.
         */
        float32 measureViewLag() const;
        /** @brief 따라가는 유닛입니다(배정 전이면 무효). */
        SlotHandle getUnitID() const { return _unitID; }
        /** @brief 한 번이라도 자리를 맞췄으면(보이기 시작했으면) true 입니다. */
        bool isPlaced() const { return _bPlaced == SW_TRUE; }

    private:
        PROPERTY( Category = "Unit", DisplayName = "Air Height", Tooltip = "Flying units hover this high", Units = m )
        float32 _airHeight;
        PROPERTY( Category = "Unit", DisplayName = "Footprint Fill", Tooltip = "A building model is scaled to this fraction of the tiles it covers", Min = 0.0, Units = ratio )
        float32 _footprintFill;
        PROPERTY( Category = "Unit", DisplayName = "Unit Fill", Tooltip = "A unit model is scaled to this many times its collision diameter (a bit larger reads as a ship from above)", Min = 0.0 )
        float32 _unitFill;

        SW_TRANSIENT float3     _lastPosition; ///< 지난 프레임 자리(움직인 쪽으로 돌린다)
        SW_TRANSIENT SlotHandle _unitID;
        SW_TRANSIENT float32    _modelWidth;
        SW_TRANSIENT float32    _yaw;
        SW_TRANSIENT uint8      _bPlaced  : 1; ///< 한 번이라도 자리를 맞췄다(그 전에는 숨어 있다)
        [[maybe_unused]] uint8  _reserved : 7;
    };
} // namespace sw
