/**
 * @file GameFeatureReceiverComponent.h
 * @brief 이 오브젝트가 켜진 게임 기능의 컴포넌트를 받습니다 — 키트를 켜면 그 키트의 컴포넌트가 이름이 맞는 오브젝트에 붙고, 끄면 떨어집니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFeatureComponents;
    struct GameFeatureComponentRule;

    /**
     * @class GameFeatureReceiverComponent
     * @brief 받는 이름(`_receiver`)이 같은 규칙(`GameFeatureScope::addComponent`)의 컴포넌트를 시작할 때 붙입니다. 파일 머리말 참고.
     * @details 이미 같은 형식의 컴포넌트가 있으면 붙이지 않는다 — 프리팹이 저작한 값이 이긴다. 그런 컴포넌트는 기능을 꺼도 떼지 않는다.
     *          게임 인스턴스가 없는 월드(에디터 편집 씬)에서는 아무것도 하지 않는다.
     *          붙인 컴포넌트는 파생이다(`Component::setTransient`) — 세이브 · 스냅샷에 들지 않고, 불러온 뒤 받는 쪽이 시작하며 다시 붙인다.
     */
    REFLECT( Category = "Framework", DisplayName = "Game Feature Receiver", Tooltip = "Receives the components that active game features add to objects with this receiver name" )
    class SW_GF_API GameFeatureReceiverComponent : public Component
    {
    public:
        REFLECT_BODY();

        GameFeatureReceiverComponent();
        ~GameFeatureReceiverComponent() override = default;

        void onBeginPlay() override;
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 받는 이름을 정합니다(코드로 만든 오브젝트 · 테스트 — 시작하기 전에). 씬에 놓은 것은 PROPERTY 가 채웁니다. */
        void                 setReceiver( const hashed_string& receiver ) { _receiver = receiver; }
        const hashed_string& getReceiver() const { return _receiver; }

        /** @brief 이 받는 쪽을 든 목록입니다(`GameFeatureComponents` 만 부른다 — 사라지는 목록은 nullptr 로 푼다). */
        void bindComponents( GameFeatureComponents* pComponents ) { _pComponents = pComponents; }
        /** @brief 규칙 @p rule 의 컴포넌트를 이 오브젝트에 붙입니다. 이미 있으면 붙이지 않고, 만들 수 없는 형식이면 경고합니다. 틱 밖에서만 부른다. */
        void addFeatureComponent( const GameFeatureComponentRule& rule );
        /** @brief 규칙 @p ruleID 가 붙인 컴포넌트를 뗍니다(이미 사라졌으면 할 일이 없다). */
        void removeFeatureComponent( uint32 ruleID );
        /** @brief 지금 붙어 있는 기능 컴포넌트 수입니다. */
        uint32 getAddedCount() const { return static_cast<uint32>( _listAdded.size() ); }

    private:
        /** @brief 규칙 하나가 붙인 컴포넌트입니다. */
        struct AddedEntry
        {
            uint32          _ruleID{ 0 };
            ComponentHandle _component{};
        };

        PROPERTY( Category = "Game Feature", DisplayName = "Receiver", Tooltip = "Receiver name the game features use (Player, Combatant ...)", Required )
        hashed_string _receiver;

        SW_TRANSIENT vector<AddedEntry> _listAdded;   ///< 실행 중 상태 — 저장하지 않는다
        GameFeatureComponents*          _pComponents; ///< 시작할 때 든 목록(빌림). 시작 전이거나 목록이 사라졌으면 nullptr
    };
} // namespace sw
