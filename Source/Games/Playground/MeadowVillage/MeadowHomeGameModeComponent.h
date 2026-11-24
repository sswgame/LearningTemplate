/**
 * @file MeadowHomeGameModeComponent.h
 * @brief 집 디렉터 — 같은 오브젝트(판 상태)의 하우징 부지에 자동 플레이가 집을 한 단계씩 짓습니다. 부지 API 를 그대로 부르므로 네트워크 클라이언트에서는 서버 요청이 된다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Radians.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    class HousingPlotComponent;
    struct HousingPartDef;

    /** @struct MeadowBuildStep @brief 지을 부품 하나 — 부품 id 와 날 변환(부지 기준 XZ · 층 · 요)입니다. 스냅은 부지가 한다(씬 데이터). */
    REFLECT()
    struct MeadowBuildStep
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Part id in the plot's housing part catalog" )
        CatalogKey<HousingPartDef> _partID{};
        PROPERTY( Tooltip = "Raw plot-local XZ (m); the plot snaps it" )
        float2 _position{ 0.0f, 0.0f };
        PROPERTY( Min = 0 )
        int32 _level{ 0 };
        PROPERTY()
        Radians _yaw{};
    };

    /**
     * @class MeadowHomeGameModeComponent
     * @brief 집 한 채입니다. 같은 오브젝트의 `HousingPlotComponent` 를 쓰며, 자동 플레이일 때만 `_stepInterval` 마다 다음 단계를 놓는다.
     * @details 상태는 다음 단계 번호 하나다(배치 · 지갑은 부지가 싣는다). 네트워크 클라이언트면 먼저 부지를 차지하고(서버가 판정), 답이 오기 전에는 다음 단계를 보내지 않는다.
     *          차지가 거절되면(남의 부지) 짓지 않는다. 서버 월드는 짓지 않는다(사람이 없다 — 짓는 쪽은 클라이언트다).
     */
    REFLECT( Category = "Meadow", DisplayName = "Meadow Home Game Mode", Tooltip = "Builds the house on this object's housing plot during autoplay, one step at a time through the plot API" )
    class MeadowHomeGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "MVHM" );
        static constexpr uint32 kStateVersion = 1;

        MeadowHomeGameModeComponent();
        virtual ~MeadowHomeGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        int32 getNextStep() const { return _nextStep; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            (void)manager;
            (void)bRespawnViews;
        }

    private:
        HousingPlotComponent* findPlot() const;

    private:
        PROPERTY( Category = "Home", DisplayName = "Build Steps", Tooltip = "Parts the autoplay places on the plot, in order" )
        vector<MeadowBuildStep> _listStep;
        PROPERTY( Category = "Home", DisplayName = "Step Interval", Min = 0.0, Units = s, Tooltip = "Time between two autoplay build steps" )
        float32 _stepInterval;

        float32 _stepTimer;
        PROPERTY( RuntimeState, Min = 0 )
        int32                  _nextStep;
        uint8                  _bClaimSent : 1; ///< 네트워크 클라이언트 — 차지 요청을 보냈다(실행 동안만)
        uint8                  _bGaveUp    : 1; ///< 차지가 거절됐다 — 짓지 않는다
        [[maybe_unused]] uint8 _reserved   : 6;
    };
} // namespace sw
