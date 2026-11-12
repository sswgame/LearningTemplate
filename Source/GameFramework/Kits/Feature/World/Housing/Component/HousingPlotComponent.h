/**
 * @file HousingPlotComponent.h
 * @brief 부지 하나를 씬에 둡니다 — 배치 규칙(`HousingLayout`)과 지갑을 들고, 배치마다 부품 프리팹을, 미리보기마다 고스트 상자를 세웁니다.
 * @details 부지 원점은 이 오브젝트의 월드 자리(축 정렬)이고 칸은 +X · +Z 로 뻗는다. 상태(배치 · 지갑)는 `GameModeComponent` 의 상태 바이트로 세이브 · 핫 리로드를 넘고,
 *          세운 프리팹은 상태에서 다시 세우는 뷰라 저장 전에 걷는다(`StatefulComponentRegistrar` — 게임마다 등록하지 않는다).
 *          인게임 건축 모드(입력 · 카메라)는 게임이 이 API(`previewPlacement` · `place` · `undo` …)를 부르는 쪽이다. 에디터 저작은 맵 검사(`REFLECT( Validate )`)로 부지 경계와
 *          카탈로그의 스냅 지점을 본다 — 언리얼 Map Check · 유니티 `OnValidate` 자리.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Housing/Rule/HousingLayout.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

namespace sw
{
    class PrefabAsset;
    class ValidationContext;

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

        static constexpr uint32 kStateTag     = FourCCUtil::make( "HPLT" );
        static constexpr uint32 kStateVersion = 1;

        HousingPlotComponent();
        ~HousingPlotComponent() override;

        /** @brief 날 후보를 검증하고 고스트를 그 결과(스냅한 자리 · 초록 / 빨강)로 세웁니다. */
        HousingPlaceCheck previewPlacement( const HousingCandidate& candidate );
        /** @brief 고스트를 걷습니다. */
        void               clearPreview();
        HousingPlaceResult place( const HousingCandidate& candidate, uint32& outPlacementID );
        HousingPlaceResult remove( uint32 placementID );
        HousingPlaceResult move( uint32 placementID, const HousingTransform& rawTransform, HousingPlacementMode mode );
        HousingPlaceResult undo();
        HousingPlaceResult redo();
        /** @brief 월드 자리를 부지 기준 XZ 로 바꿉니다. */
        float2 computeLocalPosition( const float3& worldPosition ) const;

        /** @brief 부지 상태(배치 · 지갑)를 파일에 씁니다. 실패하면 까닭을 남기고 false 입니다. */
        [[nodiscard]] bool saveToFile( string_view absolutePath ) const;
        /** @brief 파일의 부지 상태로 바꿉니다(뷰는 다음 틱에 다시 세운다). 읽지 못하면 까닭을 남기고 지금 상태를 그대로 둡니다. */
        [[nodiscard]] bool loadFromFile( string_view absolutePath );

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

        void serializeState( StateIO& io ) override;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onRegister( GameObjectManager& manager ) override;
        void               onUnregister( GameObjectManager& manager ) override;
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

        shared_ptr<const HousingPartCatalog> _pCatalog;
        HousingLayout                        _layout;
        Wallet                               _wallet;
        vector<PartView>                     _listView;
        HousingCandidate                     _ghostCandidate;
        HousingPlaceCheck                    _ghostCheck;
        GameObjectHandle                     _ghost;
        uint32                               _viewRevision; ///< 뷰가 맞춘 레이아웃 판
        HousingPlaceResult                   _lastResult;
        uint8                                _bGhostShown : 1; ///< 미리보기를 켰다
        uint8                                _bGhostDirty : 1; ///< 고스트를 다시 세울 차례다
        uint8                                _reserved    : 6;
    };
} // namespace sw
