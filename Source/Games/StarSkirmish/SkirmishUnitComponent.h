/**
 * @file SkirmishUnitComponent.h
 * @brief 유닛 하나의 모습 — 디렉터 판의 같은 id 유닛을 따라 자리 · 크기(지은 만큼 · 남은 자원만큼) · 방향 · 편 색 · 고름을 맞춥니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 유닛 정의 → Kenney Space Kit 모델 이름 · 모델의 가로 폭(가장 넓은 수평 축)입니다. */
    struct SkirmishUnitModel
    {
        const utf8* _pUnitID;
        const utf8* _pModel;
        float32     _width;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkirmishUnitComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다.
     * @details 모델은 디렉터가 스폰할 때 겁니다(유닛 종류마다 정해져 있다). 움직이는 유닛은 움직인 쪽을 보고(키트 모델의 앞이 +Z) 멈추면 마지막 방향을 지킵니다.
     *          유닛이 죽거나 안개에 가려지면 디렉터가 오브젝트를 지웁니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "Skirmish Unit View", Tooltip = "Follows one unit of the skirmish director match" )
    class SkirmishUnitComponent : public Component
    {
    public:
        REFLECT_BODY();

        SkirmishUnitComponent();
        virtual ~SkirmishUnitComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터 · 유닛 · 모델 폭을 정합니다(디렉터가 스폰한 뒤 부른다). */
        void assignUnit( GameObjectHandle director, SlotHandle unitID, float32 modelWidth );

        /** @brief 유닛 정의의 모델입니다. 그리지 않는 유닛이면 nullptr 입니다. */
        static const SkirmishUnitModel* findUnitModel( const hashed_string& unitID );
        static string                   makeModelPath( const utf8* pName );

    private:
        PROPERTY( Category = "Unit", DisplayName = "Director", Tooltip = "Object with the SkirmishDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Unit", DisplayName = "Air Height", Tooltip = "Flying units hover this high", Units = m )
        float32 _airHeight;

        float3      _lastPosition; ///< 지난 프레임 자리(움직인 쪽으로 돌린다)
        SlotHandle  _unitID;
        const void* _pShownLook; ///< 지금 입은 모습(인스턴스 주소 — 비교만 한다)
        float32     _modelWidth;
        float32     _yaw;
        uint8       _bPlaced  : 1; ///< 한 번이라도 자리를 맞췄다(그 전에는 숨어 있다)
        uint8       _reserved : 7;
    };
} // namespace sw
