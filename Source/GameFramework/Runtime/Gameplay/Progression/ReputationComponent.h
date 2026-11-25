/**
 * @file ReputationComponent.h
 * @brief 플레이어 한 명의 세력 평판(`ReputationState`)을 조종자 오브젝트에 두는 컴포넌트입니다. 언리얼 `APlayerState` 에 붙는 기능 컴포넌트 자리입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"

namespace sw
{
    class GameObjectManager;
    class ReputationCatalog;

    /**
     * @class ReputationComponent
     * @brief 플레이어 한 명의 평판입니다. 조종자 오브젝트에 다른 플레이어 상태 컴포넌트와 함께 둡니다.
     * @details - **열기**: 평판 카탈로그(세력 정의)를 받아 연다(디렉터의 `startGame` — `PlayerComponentUtil::openOnOwner`). 카탈로그는 소유하지 않고 참조만 한다.
     *          - **틱**: 날이 넘어가면(월드 상태의 시계 알림) 평판을 한 번 식힌다. 참조해서 쓰는 키트는 식히지 않는다 — 키트 수만큼 거듭 식지 않게.
     *            시계 알림을 쓰는 월드 상태(`PrePhysics`)의 다음 그룹에서 읽는다 — 다른 오브젝트(조종자)의 같은 그룹 틱은 동시에 돌아 데이터 경쟁이다. 디렉터의 규칙은 틱 뒤에 돌아
     *            식힌 평판을 읽는다.
     *          - **찾기 · 세이브**: `PlayerComponentUtil` · `DeferredStateRestore`. 정의한 .cpp 가 `StatefulComponentRegistrar` 로 모든 게임의 스냅샷에 올린다.
     */
    REFLECT( Category = "Framework", DisplayName = "Reputation", Tooltip = "One player's faction reputation, on the controller object (or the director object in a scene without a controller)" )
    class SW_GF_API ReputationComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( ReputationComponent );

        static constexpr TickGroup kTickGroup = TickGroup::PostPhysics;

        static constexpr FormatDef kStateFormat{ "REPC" };

        ReputationComponent();
        ~ReputationComponent() override;

        /**
         * @brief 평판을 엽니다. 첫 호출만 카탈로그를 쓰고, 들고 있던 복원 바이트가 있으면 그것으로 복원합니다.
         * @return 새 판, 복원, 이미 열림 중 하나입니다.
         */
        [[nodiscard]] GameStateInitResult initialize( const ReputationCatalog& catalog );

        /** @brief `_bOpenAtBeginPlay` 면 카탈로그 없이 엽니다. */
        void onBeginPlay() override;
        /** @brief 열렸으면 이번 틱에 월드 상태의 날이 넘어간 수만큼 평판을 식힙니다. 다시 읽힌 카탈로그가 있으면 틱 뒤에 다시 묶습니다. */
        void onTick( TickContext& context ) override;

        /** @brief 평판 구간 하나를 씁니다. 열리기 전이면 들고 있던 바이트를 그대로 씁니다. `ComponentStateCodec::captureObject` 가 부릅니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief 열리기 전이면 들고 있다가 `initialize` 에서, 열린 뒤면 바로 적용합니다. `ComponentStateCodec::restoreObject` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        bool                   isOpen() const { return _restore.isOpen(); }
        bool                   isFreshGame() const { return _restore.isFreshGame(); }
        ReputationState&       getReputation() { return _reputation; }
        const ReputationState& getReputation() const { return _reputation; }

    private:
        /** @brief 평판을 지금 판의 카탈로그에 다시 묶습니다. */
        void rebindCatalog();

    private:
        SharedAssetRef<ReputationCatalog>   _catalog;       ///< 연 카탈로그(다시 읽히면 새 판을 가리킨다). 카탈로그 없이 열었으면 비었다
        shared_ptr<const ReputationCatalog> _pBoundCatalog; ///< 평판이 가리키는 판
        SW_CODEC_STATE ReputationState      _reputation;
        SW_CODEC_STATE DeferredStateRestore _restore;
        PROPERTY( Tooltip = "Open without a reputation catalog at begin play - only for scenes without a kit director (the hub); a director opens it with its own catalog" )
        uint8                  _bOpenAtBeginPlay : 1;
        [[maybe_unused]] uint8 _reserved         : 7;
    };
} // namespace sw
