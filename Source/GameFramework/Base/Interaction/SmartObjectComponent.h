/**
 * @file SmartObjectComponent.h
 * @brief 스마트 오브젝트 — 오브젝트 위의 자리(벤치 자리 · 엄폐 지점 · 작업대 앞)를 플레이어와 AI 가 같은 데이터 · 같은 함수로 차지하고 비웁니다.
 * @details 언리얼 Smart Objects · 심즈의 오브젝트 광고와 같은 자리입니다. 자리 정의는 상호작용 표의 `<SmartObject>` 이고(`InteractionCatalog`),
 *          차지는 오브젝트 핸들로 셉니다. 자리를 고르는 AI 는 태그(`Activity.Sit`)로 빈자리를 찾고, 플레이어는 상호작용으로 같은 자리를 차지합니다.
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

#include "GameFramework/Base/Interaction/InteractionCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class SmartObjectSlots
     * @brief 자리마다 차지한 이(오브젝트 id, 0 = 빔)입니다. 씬 없이 시험합니다.
     */
    class SW_GF_API SmartObjectSlots
    {
    public:
        /** @brief 정의의 자리 수만큼 비웁니다. */
        void initialize( const SmartObjectDef& def );

        /** @brief 비어 있으면 차지합니다. 이미 그 자리를 가졌으면 true(멱등), 다른 이가 가졌으면 false 입니다. 한 이는 한 자리만 가집니다. */
        [[nodiscard]] bool claim( int32 slot, uint64 claimantId );
        /** @brief 그 이가 가진 자리를 비웁니다. 가진 자리가 없으면 false 입니다. */
        bool release( uint64 claimantId );
        /** @brief @p requiredTags 를 모두 가진 빈자리 중 앞의 것입니다. 없으면 −1 입니다. */
        int32  findFreeSlot( const TagContainer& requiredTags ) const;
        int32  findSlotOf( uint64 claimantId ) const;
        uint64 getClaimant( int32 slot ) const;
        bool   isFree( int32 slot ) const { return getClaimant( slot ) == 0; }
        int32  getSlotCount() const { return static_cast<int32>( _listClaimant.size() ); }
        int32  countFree() const;

        /** @brief 자리 정의입니다(태그 · 오프셋). */
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
     * @brief `_smartObjectId` 의 자리를 이 오브젝트에 둡니다. 차지 · 비움은 여러 스레드(병렬 틱의 AI)에서 불려도 됩니다(잠금).
     * @details 차지한 이의 핸들 목록(`_listClaimant`, 자리 순서)은 PROPERTY 라 세이브 · 핫 리로드를 넘깁니다. 차지한 이가 사라지면 다음 찾기에서 비웁니다.
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
        /** @brief 상호작용 표를 다시 읽었으면(핫 리로드) 새 표에서 자리 정의를 다시 찾습니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 정의 id · 표 경로 · 차지 목록을 고치면 다시 찾습니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 정의를 코드로 넣습니다(표보다 우선). */
        void setDefinition( const SmartObjectDef& def );
        /** @brief 정의 id 를 바꾸고 다시 찾습니다. */
        void setSmartObjectId( const hashed_string& id );
        /** @brief 상호작용 표 경로를 바꾸고 다시 찾습니다(비면 공용 기본 표). */
        void setCatalogPath( string_view path );
        /** @brief 자리를 차지합니다. */
        [[nodiscard]] bool claimSlot( const GameObject& claimant, int32 slot );
        /** @brief 태그에 맞는 빈자리를 찾아 차지합니다. 차지한 자리 번호, 없으면 −1 입니다. */
        int32 claimFreeSlot( const GameObject& claimant, const TagContainer& requiredTags );
        /** @brief 그 이의 자리를 비웁니다. */
        bool  releaseSlot( const GameObject& claimant );
        int32 findSlotOf( const GameObject& claimant ) const;
        int32 countFreeSlots() const;
        int32 getSlotCount() const;
        /** @brief 자리의 월드 자리 · 방향(요, 라디안)입니다 — 앉거나 숨을 자리로 걸어가 맞춘다. */
        bool computeSlotTransform( int32 slot, float3& outPosition, float32& outYaw ) const;

    private:
        void resolveDefinition();
        /** @brief 사라진 차지한 이를 비우고 핸들 목록을 맞춥니다(잠금 안에서). */
        void syncClaimants();

    private:
        PROPERTY( Category = "Smart Object", DisplayName = "Smart Object", Tooltip = "Smart object id in the interaction catalog" )
        hashed_string _smartObjectId;
        PROPERTY( Category = "Smart Object", DisplayName = "Catalog", AssetPath, Tooltip = "Interaction table; empty uses the common default" )
        string _catalogPath;
        PROPERTY( Category = "Smart Object", DisplayName = "Claimants", Tooltip = "Who holds each slot (runtime)" )
        vector<GameObjectHandle> _listClaimant;

        SmartObjectSlots   _slots;
        mutable std::mutex _mutex;
        uint32             _seenCatalogReloadCount; ///< 정의를 찾을 때의 `InteractionCatalog::getSharedReloadCount` — 달라지면 다시 찾는다
        uint8              _bHasOverride;
    };
} // namespace sw
