/**
 * @file MeadowNetSimGame.cpp
 * @brief 초원 마을 미니게임(`minigames/meadowvillage`)의 맵을 한 프로세스 가상 서버(`NetSimHarness`, 에디터 Multiplayer 창의 In process 탭)에서 돌리는 `INetSimGame` 입니다 — 하우징 서버 권위 배치를 둘 이상의 월드로 본다.
 * @details 월드마다 마을 씬(미니게임 `meadowvillage` 의 맵)을 그 월드의 씬에 풀어 넣는다. 씬의 하우징 부지와 `HousingNetSessionComponent` 는 월드의 넷 링크로
 *          역할을 받으므로 서버 월드는 판정 · 방송하고 클라이언트 월드는 요청을 보낸다(자동 플레이의 집 디렉터가 짓는다). 늦게 붙은 클라이언트 월드도 같은 길로 서고
 *          다음 서버 틱에 부지 상태를 받는다. 이 게임의 세션은 가진 메시지가 없다 — 복제는 하우징 키트의 처리기가 넷 링크의 라우터에 직접 단다.
 *          게임 모듈이 올라오면 `NetSimGameRegistry` 에 등록하고, 내려가기 전에 뺀다.
 */
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"
#include "GameFramework/Kits/Feature/World/Housing/Component/HousingPlotComponent.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameDefinition.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    /** @brief 초원 마을의 한 프로세스 재생 게임입니다. 파일 머리말 참고. */
    class MeadowNetSimGame final : public INetSimGame
    {
    public:
        static constexpr const utf8* kName         = "MeadowVillage: housing"; ///< `NetSimGameRegistry` 에 등록하는 이름
        static constexpr const utf8* kMinigame     = "meadowvillage";          ///< 맵을 가진 미니게임(`minigames/meadowvillage.minigame.xml`)
        static constexpr float32     kViewTargetX  = 4.0f;                     ///< 밭과 집 부지 사이(m)
        static constexpr float32     kViewTargetZ  = -2.0f;                    ///< 집 부지(z -7 ~ -1)와 밭(z 0 ~ 2) 사이(m)
        static constexpr float32     kViewDistance = 24.0f;
        static constexpr float32     kViewPitch    = 0.9f; ///< 씬의 게임 카메라와 비슷한 내려다보기(라디안)

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override;
        /** @brief 마을을 내려다보는 카메라를 알립니다(씬이 자기 땅을 가지므로 바닥은 깔지 않는다). */
        bool describePreviewFraming( NetSimPreviewFraming& outFraming ) const override;

        /** @brief `NetSimGameRegistry` 에 등록할 생성 함수입니다. */
        static unique_ptr<INetSimGame> create();
    };
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @class MeadowNetSimSession
         * @brief 월드 하나의 초원 마을 세션입니다 — 메시지는 없고, 시나리오가 볼 부지 값을 낸다(`Editor.NetSimValue`).
         */
        class MeadowNetSimSession final : public INetSimSession
        {
        public:
            explicit MeadowNetSimSession( GameObjectManager& manager )
                : _pManager{ &manager }
            {
            }

            /**
             * @brief 이 월드의 열린 하우징 부지(월드에 하나) 값입니다. `PlacementCount` · `ViewCount` · `Funds` · `Owned`(이 월드의 플레이어가 주인이면 1) · `PendingRequests` ·
             *        `LastResult`(`HousingPlaceResult` 의 수 — 열린 부지가 없으면 값이 없다).
             */
            bool readValue( string_view key, float64& outValue ) const override
            {
                const HousingPlotComponent* pPlot = findPlot();
                if ( pPlot == nullptr )
                    return false;
                if ( key == "PlacementCount" )
                    outValue = static_cast<float64>( pPlot->getLayout().getPlacements().size() );
                else if ( key == "ViewCount" )
                    outValue = static_cast<float64>( pPlot->getViewCount() );
                else if ( key == "Funds" )
                    outValue = static_cast<float64>( pPlot->getFunds() );
                else if ( key == "Owned" )
                    outValue = pPlot->isOwnedLocally() ? 1.0 : 0.0;
                else if ( key == "PendingRequests" )
                    outValue = static_cast<float64>( pPlot->getPendingRequestCount() );
                else if ( key == "LastResult" )
                    outValue = static_cast<float64>( static_cast<uint8>( pPlot->getLastResult() ) );
                else
                    return false;
                return true;
            }

        private:
            const HousingPlotComponent* findPlot() const
            {
                // 마을 씬의 열린 부지는 월드에 하나다 — 둘이면 오류로 값이 없다(등록 순서로 고르지 않는다).
                return _pManager->getComponentRegistry().findUnique<HousingPlotComponent>( []( const HousingPlotComponent& plot )
                { return plot.isStarted(); } );
            }

        private:
            GameObjectManager* _pManager; ///< 이 월드의 매니저(월드가 세션보다 오래 산다)
        };

        /** @brief 게임 모듈이 로드된 동안 이 게임을 한 프로세스 재생의 게임 목록에 올립니다(등록과 해제를 한 객체의 생성자 · 소멸자에 둔다 — `NetSimGameRegistry`). */
        struct MeadowNetSimRegistrar
        {
            NetSimGameEntry _entry{ MeadowNetSimGame::kName, &MeadowNetSimGame::create };
            MeadowNetSimRegistrar() { (void)NetSimGameRegistry::registerGame( _entry ); }
            ~MeadowNetSimRegistrar() { (void)NetSimGameRegistry::unregisterGame( _entry ); }
        };

        const MeadowNetSimRegistrar s_meadowNetSimRegistrar;
    } // namespace

    unique_ptr<INetSimSession> MeadowNetSimGame::createSession( NetSimWorld& world )
    {
        GamePackConfig packConfig{};
        packConfig._packRoot = game::getSubsystem<const GameConfig>()._packRoot;
        MinigameDefinition definition{};
        if ( MinigameFlow::loadDefinitionFile( packConfig, kMinigame, definition ) == false )
            return nullptr; // 이유는 읽기가 남겼다
        const string& scenePath = definition._map.get();
        SceneDocument document{};
        if ( document.load( AssetID{ scenePath } ) == false )
        {
            SW_LOG_ERROR_ID( "MeadowNetSimGame.SceneNotLoaded", "Meadow net sim: the village scene '%#' could not be loaded - the world stays empty", scenePath );
            return nullptr;
        }
        if ( world.getScene().instantiate( document ) == false )
        {
            SW_LOG_ERROR_ID( "MeadowNetSimGame.SceneNotInstantiated", "Meadow net sim: the village scene '%#' could not be instantiated - the world stays incomplete", scenePath );
            return nullptr;
        }
        return make_unique<MeadowNetSimSession>( world.getObjectManager() );
    }

    bool MeadowNetSimGame::describePreviewFraming( NetSimPreviewFraming& outFraming ) const
    {
        outFraming._target    = float3{ kViewTargetX, 0.0f, kViewTargetZ };
        outFraming._distance  = kViewDistance;
        outFraming._yaw       = 0.0f;
        outFraming._pitch     = kViewPitch;
        outFraming._floorSize = 0.0f;
        return true;
    }

    unique_ptr<INetSimGame> MeadowNetSimGame::create()
    {
        return make_unique<MeadowNetSimGame>();
    }
} // namespace sw
