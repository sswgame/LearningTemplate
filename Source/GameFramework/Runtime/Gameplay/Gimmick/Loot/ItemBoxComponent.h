/**
 * @file ItemBoxComponent.h
 * @brief 닿은 것 · 상호작용한 것에 시드를 고정한 가중치 뽑기로 아이템을 주고 다시 나타나는 아이템 상자입니다(보물 상자 · 낚시터도 이것이다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 상자를 무엇으로 여는지입니다. */
    ENUM()
    enum class ItemBoxOpenBy : uint8
    {
        Touch = 0, ///< 닿으면 연다(카트의 아이템 상자)
        Use        ///< 같은 오브젝트의 상호작용(`InteractableComponent`)을 끝낸 쪽에 연다(보물 상자, 낚시터)
    };

    /** @brief 아이템 상자의 후보 하나입니다. */
    REFLECT()
    struct SW_GF_API ItemBoxChoice
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _item{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
        PROPERTY( Min = 1 )
        int32 _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemBoxComponent
     * @brief 닿은 것 · 상호작용을 끝낸 것(태그로 거름)이 받을 가방(`IItemReceiver`)에 후보 중 가중치로 뽑은 아이템을 넣고(`GimmickUtil::giveItem`), `_respawnDelay` 뒤에 다시 열립니다.
     * @details 보물 상자는 `_openBy = Use` · `_maxOpenCount = 1` · `_bHideWhenOpened = false`(연 채 남는다), 낚시터는 `Use` · `_respawnDelay = 0`(늘 있다)이다. 쓸 수 없는 동안 상호작용도 끈다.
     *          뽑기는 시드(`_seed`)와 지금까지 연 횟수로 정한다. 같은 순서로 열면 같은 아이템이 나오므로 리플레이와 롤백에 쓸 수 있다. 순위 가중치 같은 게임 규칙은
     *          게임의 아이템 규칙(카트 경기 `KartRace` 의 아이템)이 자기 상자로 정한다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Item Box", Tooltip = "Gives a seeded weighted item on touch or use (item box, chest, fishing spot), hides and respawns" )
    class SW_GF_API ItemBoxComponent : public Component
    {
    public:
        REFLECT_BODY();

        ItemBoxComponent();
        virtual ~ItemBoxComponent() override = default;

        /** @brief 겹침 진입 사건을 구독합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief `Use` 면 같은 오브젝트의 상호작용 완료를 듣습니다. 상호작용이 없으면 오류입니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        void stepOnce();
        /** @brief @p target 에게 엽니다. 겹침 · 상호작용 완료가 부릅니다. 비어 있거나(숨은 동안) 받을 가방이 없거나 차면 열지 않고 무효 이름을 반환합니다. 게임 스레드(틱 밖)에서 부릅니다. */
        hashed_string open( const GameObject& target );
        void          setChoices( const vector<ItemBoxChoice>& listChoice ) { _listChoice = listChoice; }

        void setMaxOpenCount( int32 maxOpenCount ) { _maxOpenCount = maxOpenCount; }
        void setOpenBy( ItemBoxOpenBy openBy ) { _openBy = openBy; }

        bool  isAvailable() const { return _respawnStepsLeft <= 0 && ( _maxOpenCount <= 0 || _openCount < _maxOpenCount ); }
        int32 getOpenCount() const { return _openCount; }

    private:
        /** @brief 닿은 몸(`GameObjectEvents::_overlapBegin`, 태그로 거름)에게 엽니다. */
        void handleOverlapBegin( const OverlapInfo& overlap );
        /** @brief 같은 오브젝트의 상호작용을 끝낸 쪽(태그로 거름)에게 엽니다. */
        void handleUsed( const GameObject& user );

        Subscription _overlapSubscription; ///< 겹침 진입 구독(`handleOverlapBegin`)
        Subscription _usedSubscription;    ///< 상호작용 완료 구독(`handleUsed`)
        PROPERTY( Category = "Item Box", DisplayName = "Open By", Tooltip = "Touch: overlapping opens it; Use: completing this object's interaction opens it" )
        ItemBoxOpenBy _openBy;
        PROPERTY( Category = "Item Box", DisplayName = "Max Open Count", Min = 0, Tooltip = "How many times it opens in all (0: no limit)" )
        int32 _maxOpenCount;
        PROPERTY( Category = "Item Box", DisplayName = "Hide When Opened", Tooltip = "Hide the body until it respawns (off: the body stays, as an opened chest)" )
        bool _bHideWhenOpened;
        PROPERTY( Category = "Item Box", DisplayName = "Choices" )
        vector<ItemBoxChoice> _listChoice;
        PROPERTY( Category = "Item Box", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Item Box", DisplayName = "Respawn Delay", Min = 0.0, Units = s )
        float32 _respawnDelay;
        PROPERTY( Category = "Item Box", DisplayName = "Seed" )
        uint32 _seed;
        PROPERTY( RuntimeState, Category = "Item Box", DisplayName = "Open Count", Tooltip = "Runtime" )
        int32 _openCount;
        PROPERTY( RuntimeState, Category = "Item Box", DisplayName = "Respawn Steps Left", Tooltip = "Runtime" )
        int32 _respawnStepsLeft;

        FixedStepTimer _clock;
    };
} // namespace sw
