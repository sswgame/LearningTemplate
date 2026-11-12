#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternDeedComponent.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionView.h"

#include <algorithm>

namespace sw
{
    WesternWitnessComponent::WesternWitnessComponent()
        : _bLawman{ false }
    {
    }

    void WesternWitnessComponent::collect( const GameObjectManager& manager, vector<WesternWitness>& outListWitness )
    {
        outListWitness.clear();
        for ( WesternWitnessComponent* pWitness : manager.getComponentRegistry().getAll<WesternWitnessComponent>() )
        {
            const GameObject*     pOwner = pWitness->getOwner();
            const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr || pOwner->isActiveInHierarchy() == false )
                continue;
            WesternWitness witness;
            witness._id       = pOwner->getObjectID();
            witness._position = pScene->getWorldPosition();
            witness._forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pScene->getWorldMatrix() );
            witness._bLawman  = pWitness->_bLawman ? SW_TRUE : SW_FALSE;
            outListWitness.push_back( witness );
        }
        std::sort( outListWitness.begin(), outListWitness.end(), []( const WesternWitness& lhs, const WesternWitness& rhs )
        { return lhs._id < rhs._id; } );
    }

    WesternDeedComponent::WesternDeedComponent()
        : _kind{ WesternDeedKind::HonorAction }
        , _id{}
        , _weapon{}
        , _zone{}
        , _hitCount{ 1 }
        , _reach{ 2.5f }
        , _useCount{ 0 }
        , _bOnce{ false }
        , _bHideWhenUsed{ false }
    {
    }

    WesternDeed WesternDeedComponent::makeDeed( const float3& position ) const
    {
        WesternDeed deed;
        deed._kind     = _kind;
        deed._id       = _id;
        deed._weapon   = _weapon;
        deed._zone     = _zone;
        deed._hitCount = _hitCount;
        deed._position = position;
        return deed;
    }

    void WesternDeedComponent::markUsed()
    {
        ++_useCount;
        GameObject* pOwner = getOwner();
        if ( _bHideWhenUsed && pOwner != nullptr )
            pOwner->setActive( false );
    }

    WesternDeedComponent* WesternDeedComponent::useNearest( GameObjectManager& manager, const float3& position, WesternOutlaw& outlaw )
    {
        vector<WesternDeedComponent*> listDeed;
        for ( WesternDeedComponent* pDeed : manager.getComponentRegistry().getAll<WesternDeedComponent>() )
        {
            const GameObject* pOwner = pDeed->getOwner();
            if ( pOwner != nullptr && pOwner->isActiveInHierarchy() && pOwner->getPrimarySceneComponent() != nullptr && pDeed->canUse() )
                listDeed.push_back( pDeed );
        }
        // 방문 순서를 약속하지 않으므로 같은 거리의 자리를 id 로 정해 둔다.
        std::sort( listDeed.begin(), listDeed.end(), []( const WesternDeedComponent* pLeft, const WesternDeedComponent* pRight )
        { return pLeft->getOwner()->getObjectID() < pRight->getOwner()->getObjectID(); } );
        vector<InteractionCandidate> listCandidate;
        listCandidate.reserve( listDeed.size() );
        for ( const WesternDeedComponent* pDeed : listDeed )
        {
            InteractionCandidate candidate;
            candidate._position             = pDeed->getOwner()->getPrimarySceneComponent()->getWorldPosition();
            candidate._objectID             = pDeed->getOwner()->getObjectID();
            candidate._maxDistance          = pDeed->_reach;
            candidate._bRequiresLineOfSight = SW_FALSE;
            listCandidate.push_back( candidate );
        }
        InteractionView viewer;
        viewer._position           = position;
        const optional<int32> pick = InteractionView::selectBest( viewer, listCandidate, nullptr );
        if ( pick.has_value() == false )
            return nullptr;
        WesternDeedComponent*  pPicked = listDeed[static_cast<size_t>( *pick )];
        vector<WesternWitness> listWitness;
        WesternWitnessComponent::collect( manager, listWitness );
        if ( outlaw.perform( pPicked->makeDeed( listCandidate[static_cast<size_t>( *pick )]._position ), listWitness ) == false )
            return nullptr;
        pPicked->markUsed();
        return pPicked;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Western.*` 입니다. 대상은 서부극 디렉터 오브젝트(게임 판)입니다. */
        struct WesternOutlawWatchesInternal
        {
            static const WesternOutlaw* findTargetOutlaw( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*               pTarget   = query.findTargetObject();
                const WesternGameModeComponent* pGameMode = pTarget != nullptr ? pTarget->getComponent<WesternGameModeComponent>() : nullptr;
                const WesternOutlaw*            pOutlaw   = pGameMode != nullptr ? pGameMode->findOutlaw() : nullptr;
                if ( pOutlaw == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "opened western game mode" ) );
                return pOutlaw;
            }

            /** @brief 디렉터의 값 하나를 수로 읽습니다. */
            template <typename TRead>
            [[nodiscard]] static bool readNumber( const AutomationWatchQuery& query, AutomationWatchValue& outValue, TRead read )
            {
                const WesternOutlaw* pOutlaw = findTargetOutlaw( query, outValue );
                if ( pOutlaw == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( read( *pOutlaw ) ) );
                return true;
            }

            [[nodiscard]] static bool readHonor( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getHonorValue(); } );
            }

            [[nodiscard]] static bool readBounty( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getBounty(); } );
            }

            [[nodiscard]] static bool readWanted( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getWantedLevel(); } );
            }

            [[nodiscard]] static bool readPendingReports( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.countPendingReports(); } );
            }

            [[nodiscard]] static bool readMoney( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getMoney(); } );
            }

            [[nodiscard]] static bool readCarcasses( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getCarcassCount(); } );
            }

            [[nodiscard]] static bool readPelts( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getPeltCount(); } );
            }

            [[nodiscard]] static bool readStaminaCore( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getSurvival().getCore( WesternCore::Stamina ); } );
            }

            [[nodiscard]] static bool readWarmth( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getSurvival().computeWarmth(); } );
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( westernHonor, "Western.Honor", "Honor of the western game mode on target", &WesternOutlawWatchesInternal::readHonor, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernBounty, "Western.Bounty", "Bounty in the region of the western game mode on target", &WesternOutlawWatchesInternal::readBounty, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernWanted, "Western.Wanted", "Wanted level in the region of the western game mode on target", &WesternOutlawWatchesInternal::readWanted, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernPendingReports, "Western.PendingReports", "Witnesses waiting to report on the western game mode on target", &WesternOutlawWatchesInternal::readPendingReports, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernMoney, "Western.Money", "Money in the catalog currency of the western game mode on target", &WesternOutlawWatchesInternal::readMoney, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernCarcasses, "Western.Carcasses", "Carcasses held by the western game mode on target", &WesternOutlawWatchesInternal::readCarcasses, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernPelts, "Western.Pelts", "Pelts held by the western game mode on target", &WesternOutlawWatchesInternal::readPelts, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernStaminaCore, "Western.StaminaCore", "Stamina core (0..100) of the western game mode on target", &WesternOutlawWatchesInternal::readStaminaCore, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernWarmth, "Western.Warmth", "Warmth of the clothes worn on the western game mode on target", &WesternOutlawWatchesInternal::readWarmth, true, false );

    void WesternWitnessComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<WesternWitnessComponent>( this );
    }

    void WesternWitnessComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<WesternWitnessComponent>( this );
        Component::onUnregister( manager );
    }

    void WesternDeedComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<WesternDeedComponent>( this );
    }

    void WesternDeedComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<WesternDeedComponent>( this );
        Component::onUnregister( manager );
    }
} // namespace sw
