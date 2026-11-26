#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Radians.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingPlotComponent.h"
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

        static constexpr FormatDef kStateFormat{ "MVHM" };

        MeadowHomeGameModeComponent();
        virtual ~MeadowHomeGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        int32 getNextStep() const { return _nextStep; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               updateGame( GameSeconds frameTime ) override;

    private:
        HousingPlotComponent* findPlot() const;

    private:
        PROPERTY( Category = "Home", DisplayName = "Build Steps", Tooltip = "Parts the autoplay places on the plot, in order" )
        vector<MeadowBuildStep> _listStep;
        PROPERTY( Category = "Home", DisplayName = "Step Interval", Min = 0.0, Units = s, Tooltip = "Time between two autoplay build steps" )
        float32 _stepInterval;

        SW_TRANSIENT float32 _stepTimer;
        PROPERTY( RuntimeState, Min = 0 )
        int32                  _nextStep;
        SW_TRANSIENT uint8     _bClaimSent : 1; ///< 네트워크 클라이언트 — 차지 요청을 보냈다(실행 동안만)
        SW_TRANSIENT uint8     _bGaveUp    : 1; ///< 차지가 거절됐다 — 짓지 않는다
        [[maybe_unused]] uint8 _reserved   : 6;
    };

    MeadowHomeGameModeComponent::MeadowHomeGameModeComponent()
        : _listStep{}
        , _stepInterval{ 0.25f }
        , _stepTimer{ 0.0f }
        , _nextStep{ 0 }
        , _bClaimSent{ SW_FALSE }
        , _bGaveUp{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MeadowHomeGameModeComponent::~MeadowHomeGameModeComponent() = default;

    void MeadowHomeGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( kStateFormat );
        io.check( _nextStep <= static_cast<int32>( _listStep.size() ) ); // 다음 단계는 상태 칸이다(이미 읽혔다) — 단계표와의 검사만 손으로, 거절하면 기반이 되돌린다
    }

    HousingPlotComponent* MeadowHomeGameModeComponent::findPlot() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<HousingPlotComponent>() : nullptr;
    }

    bool MeadowHomeGameModeComponent::startGame()
    {
        if ( findPlot() == nullptr )
        {
            SW_LOG_ERROR_ID( "MeadowHome.PlotMissing", "[Meadow] the home director needs a HousingPlotComponent on the same object - the house cannot be built" );
            return false;
        }
        _stepTimer  = 0.0f;
        _bClaimSent = SW_FALSE;
        _bGaveUp    = SW_FALSE;
        return true;
    }

    void MeadowHomeGameModeComponent::updateGame( GameSeconds frameTime )
    {
        const float32         deltaTime = frameTime.count();
        HousingPlotComponent* pPlot     = findPlot();
        const bool            bSkipped  = isAutoPlayOn() == false || pPlot == nullptr || pPlot->isStarted() == false || pPlot->getNetRole() == HousingNetRole::Server;
        const bool            bFinished = _bGaveUp == SW_TRUE || _nextStep >= static_cast<int32>( _listStep.size() );
        if ( bSkipped || bFinished || pPlot->getPendingRequestCount() > 0 )
            return; // 서버의 답을 기다리는 동안은 다음 단계를 보내지 않는다(받침 · 값이 앞 단계에 기댄다)
        if ( pPlot->getNetRole() == HousingNetRole::Standalone && pPlot->getOwnerID() == HousingOwnerID::kNone )
            (void)pPlot->claim(); // 혼자 하는 게임도 로컬 주체로 차지해 세이브에 주인을 싣는다
        if ( pPlot->isOwnedLocally() == false )
        {
            if ( _bClaimSent == SW_FALSE )
            {
                _bClaimSent = SW_TRUE;
                (void)pPlot->claim();
                return;
            }
            if ( pPlot->getLastResult() == HousingPlaceResult::Ok )
                return; // 서버가 받았다 — 주인 상태는 답과 따로 방송되어 늦게 올 수 있다
            _bGaveUp = SW_TRUE;
            SW_LOG_INFO_ID( "MeadowHome.PlotTaken", "[Meadow] the plot belongs to another player (%#) - this player does not build", getEnumName( pPlot->getLastResult() ) );
            return;
        }
        _stepTimer += deltaTime;
        if ( _stepTimer < _stepInterval )
            return;
        _stepTimer                  = 0.0f;
        const MeadowBuildStep& step = _listStep[static_cast<size_t>( _nextStep )];
        ++_nextStep;
        HousingCandidate candidate;
        candidate._partID                    = step._partID.key();
        candidate._transform._position       = step._position;
        candidate._transform._level          = step._level;
        candidate._transform._yaw            = step._yaw.radians();
        uint32                   placementID = 0;
        const HousingPlaceResult result      = pPlot->place( candidate, placementID );
        if ( result != HousingPlaceResult::Ok && result != HousingPlaceResult::Pending )
            SW_LOG_WARNING_ID( "MeadowHome.StepRefused", "[Meadow] build step %# ('%#') was refused: %#", _nextStep, step._partID.c_str(), getEnumName( result ) );
    }
} // namespace sw

#include "MeadowHomeGameModeComponent.cpp.gen.inl"
