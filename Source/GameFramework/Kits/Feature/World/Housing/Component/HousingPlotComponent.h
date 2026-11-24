/**
 * @file HousingPlotComponent.h
 * @brief 부지 하나를 씬에 둡니다 — 배치 규칙(`HousingLayout`)과 지갑을 들고, 배치마다 부품 프리팹을, 미리보기마다 고스트 상자를 세웁니다.
 * @details 부지 원점은 이 오브젝트의 월드 자리(축 정렬)이고 칸은 +X · +Z 로 뻗는다. 상태(배치 · 지갑)는 `GameModeComponent` 의 상태 바이트로 세이브 · 핫 리로드를 넘고,
 *          세운 프리팹은 상태에서 다시 세우는 뷰라 저장 전에 걷는다(`StatefulComponentRegistrar` — 게임마다 등록하지 않는다).
 *          인게임 건축 모드(입력 · 카메라)는 게임이 이 API(`previewPlacement` · `place` · `undo` …)를 부르는 쪽이다.
 *          네트워크 월드(씬 넷 링크가 올라 있다)에서는 같은 씬의 `HousingNetSessionComponent` 가 필요하다 — 클라이언트의 편집은 요청으로 줄에 쌓이고(`Pending`)
 *          세션이 서버에 보내 판정을 받으며, 레이아웃 · 지갑은 서버가 방송한 상태로만 바뀐다(`HousingReplication.h`). 서버 월드의 편집은 그대로 적용되고 방송된다. 에디터 저작은 맵 검사(`REFLECT( Validate )`)로 부지 경계와
 *          카탈로그의 스냅 지점을 본다 — 언리얼 Map Check · 유니티 `OnValidate` 자리.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Housing/Net/HousingReplication.h"
#include "GameFramework/Kits/Feature/World/Housing/Rule/HousingLayout.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    class HousingNetSessionComponent;
    class PrefabAsset;
    class ValidationContext;

    /** @brief 부지가 놓인 월드의 역할입니다. 시작할 때 씬 넷 링크로 정한다. */
    enum class HousingNetRole : uint8
    {
        Standalone = 0, ///< 네트워크 없음 — 편집을 바로 적용한다
        Server,         ///< 권위 — 편집을 바로 적용하고 세션이 방송한다
        Client          ///< 편집은 요청이고 상태는 서버가 보낸다
    };

    /**
     * @class HousingPlotComponent
     * @brief 파일 머리말 참고.
     */
    REFLECT( Category = "Housing", DisplayName = "Housing Plot", Tooltip = "A buildable plot: places catalog parts with ghost preview, snap, cost, refund and undo, and saves the layout",
             Validate = validatePlot )
    class SW_KIT_API HousingPlotComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( HousingPlotComponent );
        friend class HousingNetSessionComponent; ///< 세션이 틱 뒤에 요청을 꺼내 보내고 판정 · 주인을 돌려준다

        static constexpr uint32 kStateTag     = FourCCUtil::make( "HPLT" );
        static constexpr uint32 kStateVersion = 2; ///< 2: 앞에 주인 주체(`HousingOwnerID`)가 생겼다

        HousingPlotComponent();
        ~HousingPlotComponent() override;

        /** @brief 날 후보를 검증하고 고스트를 그 결과(스냅한 자리 · 초록 / 빨강)로 세웁니다. */
        HousingPlaceCheck previewPlacement( const HousingCandidate& candidate );
        /** @brief 고스트를 걷습니다. */
        void               clearPreview();
        HousingPlaceResult place( const HousingCandidate& candidate, uint32& outPlacementID );
        HousingPlaceResult remove( uint32 placementID );
        HousingPlaceResult move( uint32 placementID, const HousingTransform& rawTransform, HousingPlacementMode mode );
        FUNCTION( Category = "Edit", Exec )
        HousingPlaceResult undo();
        FUNCTION( Category = "Edit", Exec )
        HousingPlaceResult redo();
        /** @brief 부지를 차지합니다. 네트워크가 없으면 늘 내 것이라 `Ok`, 클라이언트는 서버에 요청하고 `Pending`, 서버 월드는 사람이 없어 `NotOwner` 입니다. */
        HousingPlaceResult claim();
        /** @brief 주인 주체입니다(`HousingOwnerID` — 서버 · 혼자 하는 게임). 클라이언트는 주체를 받지 않는다(`isOwnedLocally`). */
        uint64 getOwnerID() const { return _ownerID; }
        /** @brief 이 월드의 플레이어가 편집할 수 있는 부지면 true 입니다(네트워크 없음, 또는 서버가 내 것이라고 보낸 부지). */
        bool isOwnedLocally() const;
        /** @brief 답을 기다리는 요청 수입니다(클라이언트). */
        uint32         getPendingRequestCount() const { return _pendingRequestCount; }
        HousingNetRole getNetRole() const { return _netRole; }
        /** @brief 월드 자리를 부지 기준 XZ 로 바꿉니다. */
        float2 computeLocalPosition( const float3& worldPosition ) const;

        /** @brief 부지 상태(배치 · 지갑)를 파일에 씁니다. 실패하면 까닭을 남기고 false 입니다. */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absolutePath ) const;
        /** @brief 파일의 부지 상태로 바꿉니다(뷰는 다음 틱에 다시 세운다). 읽지 못하면 까닭을 남기고 지금 상태를 그대로 둡니다. */
        [[nodiscard]] bool loadFromFile( const AbsolutePath& absolutePath );

        const HousingLayout& getLayout() const { return _layout; }
        const Wallet&        getWallet() const { return _wallet; }
        int64                getFunds() const;
        /** @brief 지금 서 있는 부품 뷰 수입니다(고스트 제외). 배치 수와 같아야 한다 — 다시 세우기의 중복 · 누락을 시나리오가 본다. */
        uint32             getViewCount() const;
        HousingPlaceResult getLastResult() const { return _lastResult; }
        /** @brief 고스트가 서 있고 놓을 수 있는 자리면 true 입니다. */
        bool isGhostPlaceable() const { return _bGhostShown == SW_TRUE && _ghostCheck._result == HousingPlaceResult::Ok; }

        const HousingPlotSettings& getSettings() const { return _settings; }
        /**
         * @brief 부지의 월드 틀(원점 · 칸 한 변)을 읽습니다 — 에디터 뷰포트의 부지 경계 그리기가 씁니다(플레이 전에도).
         * @details 연 부지는 들고 있는 카탈로그, 열지 않은 부지는 카탈로그 에셋을 얻어 칸 크기를 읽는다. 루트 장면 컴포넌트나 카탈로그가 없으면 false 다(맵 검사가 까닭을 알린다).
         */
        [[nodiscard]] bool tryGetPlotGrid( float3& outOrigin, float32& outCellSize ) const;
        /** @brief 맵 검사: 카탈로그를 읽을 수 있는지, 부품마다 스냅 지점이 맞고 부지에 들어가는지, 같은 씬의 다른 부지와 겹치지 않는지 봅니다(`REFLECT( Validate )`). */
        void validatePlot( ValidationContext& context ) const;

        void serializeGameState( StateIO& io ) override;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onEndPlay() override;
        void               updateGame( float32 deltaTime ) override { (void)deltaTime; }
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override;
        void               onStateRestored( bool bRestored ) override;

    private:
        /** @brief 배치 하나의 뷰입니다. 변환이 바뀌면 다시 세운다. */
        struct PartView
        {
            uint32           _placementID{ 0 };
            HousingTransform _transform{};
            GameObjectHandle _object{};
        };

        /** @brief 결과를 남기고 뷰 맞추기를 잡습니다. */
        HousingPlaceResult finishEdit( HousingPlaceResult result );
        /** @brief 클라이언트면 요청을 줄에 쌓고 `Pending` 을 @p outResult 에 둔 뒤 true 입니다. 클라이언트가 아니면 false(바로 적용한다). */
        [[nodiscard]] bool queueRemoteEdit( const HousingEditRequest& request, HousingPlaceResult& outResult );
        // ---- 세션(`HousingNetSessionComponent`)이 틱 뒤 게임 스레드에서 부른다 ----
        HousingNetPlotRef makeNetPlot();
        /** @brief 쌓인 요청을 @p outListRequest 로 옮깁니다. */
        void takeOutgoingEdits( vector<HousingEditRequest>& outListRequest );
        /** @brief 서버의 판정 하나를 받습니다(마지막 결과 · 고스트를 맞춘다). */
        void receiveNetReply( HousingPlaceResult result );
        /** @brief 보내지 못한 요청 하나를 잊습니다. */
        void abandonNetRequest();
        /** @brief 쌓였거나 답을 기다리는 요청을 모두 버립니다(서버와 끊겼거나 세션이 닫혔다 — 답이 오지 않는다). */
        void abandonNetRequests();
        void setReplicatedOwner( HousingPlotOwner owner ) { _replicatedOwner = owner; }
        /** @brief 오브젝트 루트를 부지 변환(자리 · 요)에 둡니다. */
        void   placeObject( GameObject& object, const HousingTransform& transform ) const;
        void   syncPartViews( GameObjectManager& manager );
        void   syncGhost( GameObjectManager& manager );
        float3 getPlotOrigin() const;

    private:
        PROPERTY( Category = "Housing", DisplayName = "Plot", Tooltip = "Plot id, size in cells, levels, history and free placement" )
        HousingPlotSettings _settings;

        PROPERTY( Category = "Housing", DisplayName = "Part Catalog", Tooltip = "housingparts.xml with the parts this plot can place" )
        AssetRef<HousingPartCatalog> _partCatalog;

        PROPERTY( Category = "Housing", DisplayName = "Starting Funds", Min = 0, Tooltip = "Balance of the catalog currency when the plot opens" )
        int32 _startingFunds;

        PROPERTY( Category = "Housing", DisplayName = "Ghost Placeable Prefab", Tooltip = "Unit box (1 m, base at y 0) shown where a part can go" )
        AssetRef<PrefabAsset> _ghostPlaceablePrefab;

        PROPERTY( Category = "Housing", DisplayName = "Ghost Blocked Prefab", Tooltip = "Unit box (1 m, base at y 0) shown where a part cannot go" )
        AssetRef<PrefabAsset> _ghostBlockedPrefab;

        SharedAssetRef<HousingPartCatalog> _pCatalog;
        HousingLayout                      _layout;
        Wallet                             _wallet;
        vector<PartView>                   _listView;
        HousingCandidate                   _ghostCandidate;
        HousingPlaceCheck                  _ghostCheck;
        GameObjectHandle                   _ghost;
        EventBuffer<HousingEditRequest>    _outgoingEditBuffer;  ///< 클라이언트 — 세션이 다음 틱 뒤에 보낼 요청
        uint32                             _viewRevision;        ///< 뷰가 맞춘 레이아웃 판
        uint32                             _pendingRequestCount; ///< 클라이언트 — 보냈거나 쌓였고 아직 답이 없는 요청
        HousingPlaceResult                 _lastResult;
        uint64                             _ownerID;         ///< 주인 주체(`HousingOwnerID`) — 서버 · 혼자 하는 게임이 쓰고 세이브에 싣는다
        HousingPlotOwner                   _replicatedOwner; ///< 클라이언트 — 서버가 마지막으로 보낸 주인
        HousingNetRole                     _netRole;
        uint8                              _bGhostShown : 1; ///< 미리보기를 켰다
        uint8                              _bGhostDirty : 1; ///< 고스트를 다시 세울 차례다
        [[maybe_unused]] uint8             _reserved    : 6;
    };
} // namespace sw
