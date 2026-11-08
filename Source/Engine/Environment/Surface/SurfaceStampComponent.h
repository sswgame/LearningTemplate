/**
 * @file SurfaceStampComponent.h
 * @brief 오브젝트가 지나는 땅에 도장을 찍습니다 — 붙잡기(파묻힘 테두리 · 깔때기)나 지나온 길(끌린 자국 · 바퀴 자국).
 * @details 도장 데이터(`SurfaceStampDef`)를 칸으로 들고 표면 변형 층(`SurfaceDeformSystem`)에 찍는 반응 컴포넌트다. 지역 효과의 반응 · 탈것 바퀴 · 끌리는 물체가 같은 길을 쓴다.
 *          - `Hold`: 켜져 있는 동안 오브젝트 자리에 도장 하나를 붙잡아 따라다니고, 꺼지거나 떨어지면 놓는다(자국은 메워진다).
 *          - `Trail`: 오브젝트가 `_trailStep` 이상 움직일 때마다 지난 자리 → 이번 자리를 띠로 찍는다. `_trailBreak` 보다 먼 한 번의 이동(순간 이동)은 잇지 않는다.
 *          찍기는 표면 서비스(`SurfaceComponentSystem`)가 틱 뒤 게임 스레드에서 한 번에 한다(씬 서비스는 병렬 틱에서 부르지 않는다) — 컴포넌트는 칸과 등록만 든다.
 *          높이장 · 충돌은 고치지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Surface/SurfaceStamp.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 도장 컴포넌트가 찍는 방식입니다. */
    ENUM()
    enum class SurfaceStampMode : uint8
    {
        Hold = 0, ///< 자리에 붙잡아 따라다닌다 — 파묻힘 테두리 · 깔때기
        Trail,    ///< 지나온 길을 띠로 남긴다 — 끌린 자국 · 바퀴 자국
    };

    /**
     * @class SurfaceStampComponent
     * @brief 오브젝트 자리에 표면 변형 도장을 찍습니다(파일 머리말 참고).
     */
    REFLECT( Category = "Environment", DisplayName = "Surface Stamp", Tooltip = "Stamps the surface deform layer where the object is: held in place (sink rim, whirl) or along its path (drag, wheel track)" )
    class SW_API SurfaceStampComponent : public Component
    {
    public:
        REFLECT_BODY();

        SurfaceStampComponent();
        virtual ~SurfaceStampComponent() override = default;

        /** @brief 칸이 바뀌면 붙잡은 도장을 놓고 다음 찍기 전에 도장을 다시 검사합니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 씬의 컴포넌트 레지스트리에 오릅니다(표면 서비스가 돈다). */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 오브젝트가 꺼지면 붙잡은 도장을 놓고 지나온 길을 끊습니다. */
        void onOwnerActiveInHierarchyChanged() override;

        /** @brief 지금 붙잡은 도장이 있는지입니다. */
        bool isHolding() const { return _held.isValid(); }
        /** @brief 오브젝트 자리로 도장을 찍거나 옮깁니다(표면 서비스가 부른다, 틱 뒤 게임 스레드). */
        void applyStamp( GameObjectManager& manager );

    private:
        /** @brief 붙잡은 도장을 놓고 지나온 길을 끊습니다. */
        void removePersistentStamp( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Stamp", DisplayName = "Stamp", Tooltip = "Shape, size, depth, rim, fill speed and spin of the stamp" )
        SurfaceStampDef _stamp;
        PROPERTY( Category = "Stamp", DisplayName = "Mode", Tooltip = "Hold: one stamp follows the object. Trail: bands along the path" )
        SurfaceStampMode _mode;
        PROPERTY( Category = "Stamp", DisplayName = "Trail Step", Min = 0.01, Units = m, EditCondition = "_mode == Trail", Tooltip = "Distance moved before the next band is stamped" )
        float32 _trailStep;
        PROPERTY( Category = "Stamp", DisplayName = "Trail Break", Min = 0.01, Units = m, EditCondition = "_mode == Trail",
                  Tooltip = "A single move longer than this is a teleport: the path is not joined across it" )
        float32 _trailBreak;

        SurfaceStampHandle _held;
        float3             _lastPoint;
        uint8              _bHasLastPoint : 1;
        uint8              _bStampChecked : 1; ///< 도장 칸 사이의 규칙(`SurfaceStampDef::validate`)을 처음 찍기 전에 한 번 봤다
        uint8              _bValidStamp   : 1; ///< 그 검사를 지났다 — 틀린 도장은 오류를 한 번 남기고 찍지 않는다
        uint8              _reserved      : 5;
    };
} // namespace sw
