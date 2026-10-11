/**
 * @file SmartObjectComponent.h
 * @brief 스마트 오브젝트입니다. 오브젝트 위의 슬롯(벤치 자리, 엄폐 지점, 작업대 앞)을 플레이어와 AI 가 같은 데이터와 같은 함수로 차지하고 비웁니다.
 * @details 언리얼의 Smart Objects 와 심즈의 오브젝트 광고에 해당한다. 슬롯 정의는 상호작용 테이블의 `<SmartObject>` 이고(`InteractionCatalog`),
 *          차지한 쪽은 오브젝트 핸들로 센다. 슬롯을 고르는 AI 는 태그(`Activity.Sit`)로 빈 슬롯을 찾고, 플레이어는 상호작용으로 같은 슬롯을 차지한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class SmartObjectSlots
     * @brief 슬롯마다 차지한 쪽(오브젝트 id, 0 이면 빔)을 담습니다. 씬 없이 테스트할 수 있습니다.
     */
    class SW_GF_API SmartObjectSlots
    {
    public:
        /** @brief 정의의 슬롯 수만큼 비웁니다. */
        void initialize( const SmartObjectDef& def );

        /** @brief 비어 있으면 차지합니다. 이미 그 슬롯을 차지했으면 true 이고(멱등), 다른 쪽이 차지했으면 false 입니다. 한 쪽은 슬롯 하나만 차지할 수 있습니다. */
        [[nodiscard]] bool claim( int32 slot, uint64 claimantID );
        /** @brief 그 쪽이 차지한 슬롯을 비웁니다. 차지한 슬롯이 없으면 false 입니다. */
        bool release( uint64 claimantID );
        /** @brief @p requiredTags 를 모두 가진 빈 슬롯 중 앞의 것입니다. 없으면 −1 입니다. */
        int32  findFreeSlot( const TagContainer& requiredTags ) const;
        int32  findSlotOf( uint64 claimantID ) const;
        uint64 getClaimant( int32 slot ) const;
        bool   isFree( int32 slot ) const { return getClaimant( slot ) == 0; }
        int32  getSlotCount() const { return static_cast<int32>( _listClaimant.size() ); }
        int32  countFree() const;

        /** @brief 슬롯 정의입니다. 태그와 오프셋을 담습니다. */
        const SmartObjectSlotDef* findSlotDef( int32 slot ) const;

    private:
        SmartObjectDef _def{};
        vector<uint64> _listClaimant{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class SmartObjectComponent
     * @brief `_smartObjectID` 의 슬롯을 이 오브젝트에 둡니다. 차지하기와 비우기는 여러 스레드(병렬 틱의 AI)에서 불러도 됩니다. 잠금으로 보호합니다.
     * @details 차지한 쪽의 핸들 목록(`_listClaimant`, 슬롯 순서)은 PROPERTY 이므로 세이브와 핫 리로드를 거쳐도 유지된다. 차지한 쪽이 사라지면 다음 찾기에서 비운다.
     */
    REFLECT( Category = "Interaction", DisplayName = "Smart Object", Tooltip = "Claimable slots (bench, cover, workbench) shared by players and AI" )
    class SW_GF_API SmartObjectComponent : public Component
    {
    public:
        REFLECT_BODY();

        SmartObjectComponent();
        virtual ~SmartObjectComponent() override = default;

        void onPostLoad() override;
        void onBeginPlay() override;
        /** @brief 상호작용 테이블을 다시 읽었으면(핫 리로드) 새 테이블에서 슬롯 정의를 다시 찾습니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 정의 id, 테이블 경로, 차지 목록을 고치면 다시 찾습니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 정의를 코드로 넣습니다. 테이블보다 우선합니다. */
        void setDefinition( const SmartObjectDef& def );
        /** @brief 정의 id 를 바꾸고 다시 찾습니다. */
        void setSmartObjectID( const hashed_string& id );
        /** @brief 상호작용 테이블 경로를 바꾸고 다시 찾습니다. 비어 있으면 공용 기본 테이블을 씁니다. */
        void setCatalogPath( string_view path );
        /** @brief 슬롯을 차지합니다. */
        [[nodiscard]] bool claimSlot( const GameObject& claimant, int32 slot );
        /** @brief 태그에 맞는 빈 슬롯을 찾아 차지합니다. 차지한 슬롯의 번호를 반환하고, 없으면 −1 입니다. */
        int32 claimFreeSlot( const GameObject& claimant, const TagContainer& requiredTags );
        /** @brief 그 쪽이 차지한 슬롯을 비웁니다. */
        bool  releaseSlot( const GameObject& claimant );
        int32 findSlotOf( const GameObject& claimant ) const;
        int32 countFreeSlots() const;
        int32 getSlotCount() const;
        /** @brief 슬롯의 월드 위치와 방향(요, 라디안)입니다. 앉거나 숨을 위치로 걸어가서 맞출 때 씁니다. */
        bool computeSlotTransform( int32 slot, float3& outPosition, float32& outYaw ) const;

    private:
        void resolveDefinition();
        /** @brief 사라진 쪽이 차지한 슬롯을 비우고 핸들 목록을 맞춥니다. 잠금 안에서 부릅니다. */
        void syncClaimants();

    private:
        PROPERTY( Category = "Smart Object", DisplayName = "Smart Object", Tooltip = "Smart object id in the interaction catalog" )
        hashed_string _smartObjectID;
        PROPERTY( Category = "Smart Object", DisplayName = "Catalog", AssetPath, Tooltip = "Interaction table; empty uses the common default" )
        string _catalogPath;
        PROPERTY( Category = "Smart Object", DisplayName = "Claimants", Tooltip = "Who holds each slot (runtime)" )
        vector<GameObjectHandle> _listClaimant;

        SmartObjectSlots   _slots;
        mutable std::mutex _mutex;
        uint32             _seenCatalogReloadCount; ///< 정의를 찾았을 때의 `InteractionCatalog::getSharedReloadCount` 값. 달라지면 다시 찾는다
        uint8              _bHasOverride;
    };
} // namespace sw
