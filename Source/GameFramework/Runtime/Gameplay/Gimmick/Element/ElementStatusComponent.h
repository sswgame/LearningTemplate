/**
 * @file ElementStatusComponent.h
 * @brief 오브젝트 하나의 원소 상태(횃불 퍼즐 · 타는 상자)입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/AssetReloadWatch.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Element/ElementGrid.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Element/ElementRuleCatalog.h"

namespace sw
{
    /**
     * @class ElementStatusComponent
     * @brief 오브젝트 하나의 원소 상태입니다. 공용 원소 규칙 테이블을 따르는 1 × 1 격자이며 횃불, 타는 나무 상자, 얼음 덩어리에 씁니다. `applyStimulus( "Fire" )` 처럼 이름으로 자극을 가합니다.
     * @details 같은 오브젝트에 `GimmickSensorComponent` 가 있으면 `_signalStatus` 상태가 있는 동안 신호를 1 로 둔다. 횃불 네 개를 Signal 센서, And, 문 순서로
     *          연결하면 횃불 퍼즐이 된다. 스텝 간격은 테이블의 스텝 시간이며 결정적이다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Element Status", Tooltip = "One object's element state (torch, burning crate) from the shared element rule table" )
    class SW_GF_API ElementStatusComponent : public Component
    {
    public:
        REFLECT_BODY();

        ElementStatusComponent();
        virtual ~ElementStatusComponent() override = default;

        void onPostLoad() override;
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 자극을 이름으로 가합니다(Fire, Water, Ice, Electric). 바뀌었으면 true 입니다. 알 수 없는 이름이거나 테이블이 없으면 false 입니다. */
        [[nodiscard]] bool applyStimulus( const hashed_string& stimulus );
        /** @brief 상태가 있으면 true 입니다(Burning, Charged). */
        bool hasStatus( const hashed_string& status ) const;
        /** @brief 재질 이름을 바꿉니다. 상태는 지웁니다. */
        void setMaterial( const hashed_string& material );
        /** @brief 원소 규칙 테이블 경로를 바꾸고 다시 만듭니다. 비우면 테이블이 없어 자극이 듣지 않는다(오류로 알린다). */
        void setTablePath( string_view path );

    private:
        void rebuild();
        void publishSignal();

    private:
        PROPERTY( Category = "Element", DisplayName = "Rule Table", Tooltip = "Element rule table (required)" )
        AssetRef<ElementRuleCatalog> _tablePath;
        PROPERTY( Required, Category = "Element", DisplayName = "Material", Tooltip = "Material name in the table" )
        hashed_string _material;
        PROPERTY( Required, Category = "Element", DisplayName = "Signal Status", Tooltip = "Status that sets the gimmick sensor signal (Burning for torches)" )
        hashed_string _signalStatus;
        PROPERTY( Category = "Element", DisplayName = "Start Status", Tooltip = "Stimulus applied at play start (Fire for a lit torch)" )
        hashed_string _startStimulus;

        ElementGrid                          _grid;
        shared_ptr<const ElementRuleCatalog> _pTable;      ///< `_grid` 가 가리키는 공유 테이블(쥔 동안 산다)
        AssetReloadWatch<ElementRuleCatalog> _tableReload; ///< 테이블을 찾았을 때의 `SharedAssetCache<ElementRuleCatalog>::getReloadCount` 값. 달라지면 다시 만든다
    };
} // namespace sw
