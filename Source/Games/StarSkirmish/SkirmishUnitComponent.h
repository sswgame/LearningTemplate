/**
 * @file SkirmishUnitComponent.h
 * @brief 유닛 하나의 모습 — 디렉터 판의 같은 id 유닛을 따라 자리 · 크기(지은 만큼 · 남은 자원만큼) · 방향 · 편 색 · 고름을 맞춥니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /** @brief 유닛 정의 → Kenney Space Kit 모델 이름 · 모델의 가로 폭(가장 넓은 수평 축)입니다(`unitmodels.xml` 의 한 줄). */
    struct SkirmishUnitModel
    {
        hashed_string _id{};    ///< 유닛 카탈로그(units.xml)의 id
        string        _model{}; ///< 모델 파일 이름(확장자 없이)
        float32       _width{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `<SkirmishUnitModels><Model id="marine" model="craft_speeder_a" width="2.1"/></SkirmishUnitModels>` 를 읽습니다.
     * @details 키트 모델은 크기가 제각각이라(안테나 0.75 · 격납고 3.3) 모델마다 폭을 두고 Footprint Fill · Unit Fill 로 늘린다. 세 속성은 모두 필수다.
     */
    class SkirmishUnitModelCatalog : public GameCatalog<SkirmishUnitModel, XMLCatalog<SkirmishUnitModelCatalog>>
    {
        friend class XMLCatalog<SkirmishUnitModelCatalog>;

    public:
        /** @brief 모델 표의 모든 모델 경로입니다(겹치는 모델은 한 번). 판을 열 때 미리 읽는다. */
        vector<string> makeAllModelPaths() const;

    private:
        static constexpr const utf8* kXMLRootName = "SkirmishUnitModels";
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
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

        /**
         * @brief 이 뷰의 메시 자리와 디렉터가 지금 가진 유닛 자리의 수평 거리입니다(감시 값 `Skirmish.ViewLagMax` 가 쓴다). 비교할 수 없으면(유닛이 없거나 아직 자리를 맞추기 전) -1 입니다.
         * @details 뷰가 디렉터보다 뒤 그룹에서 돌면 한 프레임이 끝났을 때 0 이다. 앞 그룹에서 돌면 유닛이 움직인 만큼 뒤처진다 — 그룹 순서를 지키는지 시나리오가 본다.
         */
        float32 measureViewLag() const;

        static string makeModelPath( const utf8* pName );

    private:
        PROPERTY( Category = "Unit", DisplayName = "Director", Tooltip = "Object with the SkirmishDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Unit", DisplayName = "Air Height", Tooltip = "Flying units hover this high", Units = m )
        float32 _airHeight;
        PROPERTY( Category = "Unit", DisplayName = "Footprint Fill", Tooltip = "A building model is scaled to this fraction of the tiles it covers", Min = 0.0, Units = ratio )
        float32 _footprintFill;
        PROPERTY( Category = "Unit", DisplayName = "Unit Fill", Tooltip = "A unit model is scaled to this many times its collision diameter (a bit larger reads as a ship from above)", Min = 0.0 )
        float32 _unitFill;

        float3      _lastPosition; ///< 지난 프레임 자리(움직인 쪽으로 돌린다)
        SlotHandle  _unitID;
        const void* _pShownLook; ///< 지금 입은 모습(인스턴스 주소 — 비교만 한다)
        float32     _modelWidth;
        float32     _yaw;
        uint8       _bPlaced  : 1; ///< 한 번이라도 자리를 맞췄다(그 전에는 숨어 있다)
        uint8       _reserved : 7;
    };
} // namespace sw
