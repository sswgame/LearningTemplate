#include "pch.h"

#include "Games/MeadowVillage/MeadowHomeGameModeComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingPlotComponent.h"

namespace sw
{
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

    void MeadowHomeGameModeComponent::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        int32 nextStep = _nextStep;
        io.value( nextStep );
        if ( io.isReading() && io.check( nextStep >= 0 && nextStep <= static_cast<int32>( _listStep.size() ) ) )
            _nextStep = nextStep;
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

    void MeadowHomeGameModeComponent::updateGame( float32 deltaTime )
    {
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
        candidate._partID                    = step._partID;
        candidate._transform._position       = step._position;
        candidate._transform._level          = step._level;
        candidate._transform._yaw            = step._yaw.radians();
        uint32                   placementID = 0;
        const HousingPlaceResult result      = pPlot->place( candidate, placementID );
        if ( result != HousingPlaceResult::Ok && result != HousingPlaceResult::Pending )
            SW_LOG_WARNING_ID( "MeadowHome.StepRefused", "[Meadow] build step %# ('%#') was refused: %#", _nextStep, step._partID.c_str(), getEnumName( result ) );
    }
} // namespace sw
