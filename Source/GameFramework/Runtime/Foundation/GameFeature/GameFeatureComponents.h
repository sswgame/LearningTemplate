/**
 * @file GameFeatureComponents.h
 * @brief 켜진 게임 기능이 받는 쪽 오브젝트(`GameFeatureReceiverComponent`)에 붙이는 컴포넌트의 규칙 목록입니다(게임 범위).
 * @details 언리얼 Game Features 의 Add Components(`UGameFrameworkComponentManager` — 받는 쪽 액터가 스스로 받겠다고 알린다)와 같은 자리다. 다른 점: 받는 쪽을 액터 클래스가 아니라
 *          데이터의 이름(`Player` · `Combatant` …)으로 고른다 — 여기서는 오브젝트의 모양이 프리팹의 컴포넌트라 클래스로 가를 수 없다(Godot 의 그룹과 같은 생각).
 *          규칙은 기능 범위만 건다(`GameFeatureScope::addComponent`) — 끄면 범위가 규칙을 빼고, 받는 쪽은 그 규칙이 붙인 컴포넌트를 뗀다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Service/GameInstanceSubsystem.h"

namespace sw
{
    class GameFeatureReceiverComponent;

    /** @brief 기능 하나가 건 "이 이름의 받는 쪽에 이 컴포넌트" 한 줄입니다. */
    struct GameFeatureComponentRule
    {
        uint32        _ruleID{ 0 };
        hashed_string _receiver{};
        hashed_string _componentType{}; ///< 리플렉션 형식 이름 — 모듈을 다시 올려도 같은 이름이다
    };

    /**
     * @class GameFeatureComponents
     * @brief 규칙과 시작한 받는 쪽의 목록입니다. 게임 스레드 전용입니다. 받는 쪽은 시작할 때 들고 등록이 풀릴 때 빠진다(소유하지 않는다).
     */
    class SW_GF_API GameFeatureComponents final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "GameFeatureComponents";

        GameFeatureComponents();
        /** @brief 남은 받는 쪽이 이 목록을 가리키지 않게 합니다. */
        ~GameFeatureComponents() override;

        const utf8* getSubsystemName() const override { return kSubsystemName; }

        /** @brief 규칙을 더하고 시작해 있는 받는 쪽에 곧바로 붙입니다. 규칙 번호를 돌려줍니다(1 부터). */
        uint32 addRule( const hashed_string& receiver, const hashed_string& componentType );
        /** @brief 규칙을 빼고 받는 쪽마다 그 규칙이 붙인 컴포넌트를 뗍니다(없는 번호면 할 일이 없다). */
        void removeRule( uint32 ruleID );

        /** @brief 받는 쪽이 시작했습니다 — 이름이 맞는 규칙을 모두 붙입니다. */
        void addReceiver( GameFeatureReceiverComponent& receiver );
        /** @brief 받는 쪽이 사라집니다(붙인 컴포넌트는 오브젝트와 함께 사라진다). */
        void removeReceiver( const GameFeatureReceiverComponent& receiver );

        /** @brief 지금 받는 쪽들에 붙어 있는 기능 컴포넌트 수입니다(감시 값 `GameFeature.AddedComponents`). */
        uint32 countAddedComponents() const;

    private:
        vector<GameFeatureComponentRule>      _listRule;
        vector<GameFeatureReceiverComponent*> _listReceiver;
        uint32                                _nextRuleID;
    };
} // namespace sw
