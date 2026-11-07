/**
 * @file DirectorBoundComponent.h
 * @brief 디렉터(`GameModeComponent`)를 핸들로 보는 뷰 · 컨트롤러 컴포넌트의 베이스입니다.
 * @details 게임 19 형식이 `_gameMode` 칸과 매니저 찾기 · `resolve` 를 저마다 적던 것을 합쳤다. 언리얼의 `GetWorld()->GetAuthGameMode<T>()` 자리다(비교는 기억에 의존한다).
 *          리플렉션이 템플릿 기반을 다루지 못해(`REFLECT` 형식은 템플릿 인자로 칸을 낼 수 없다) 디렉터 형식은 찾을 때 받는다. 칸 이름 `_gameMode` 가 씬 · 프리팹의 키이므로 바꾸지 않는다.
 *          다른 기반을 잇는 형식(`HealthSourceComponent` · `AIControllerComponent` 파생)은 다중 상속을 하지 않고 `GameModeComponent::resolve` 를 그대로 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /**
     * @class DirectorBoundComponent
     * @brief 디렉터가 있는 오브젝트의 핸들을 들고, 매 프레임 찾아 씁니다. 포인터를 보관하지 않습니다(디렉터가 먼저 사라질 수 있다).
     */
    REFLECT( Abstract, Category = "Framework", DisplayName = "Director Bound", Tooltip = "Base of the components that read a game director through a handle" )
    class SW_GF_API DirectorBoundComponent : public Component
    {
    public:
        REFLECT_BODY();

        DirectorBoundComponent();

        /** @brief 따르는 디렉터의 오브젝트를 정합니다. 스폰한 디렉터가 부르거나 씬 데이터(PROPERTY)가 적습니다. */
        void             bindGameMode( GameObjectHandle gameMode ) { _gameMode = gameMode; }
        GameObjectHandle getGameMode() const { return _gameMode; }

    protected:
        /**
         * @brief 칸의 오브젝트에 붙은 @p TDirector 입니다. 오브젝트 · 매니저 · 디렉터가 없으면 nullptr 입니다.
         * @details 오브젝트 형식을 의존 이름으로 받아 이 헤더가 오브젝트 매니저를 include 하지 않는다. 부르는 쪽은 이미 include 했다(`GameModeComponent::resolve` 와 같다).
         */
        template <typename TDirector, typename TObject = GameObject>
        const TDirector* findDirector() const
        {
            TObject* pOwner   = getOwner();
            auto*    pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
            return pManager != nullptr ? GameModeComponent::resolve<TDirector>( *pManager, _gameMode ) : nullptr;
        }

    private:
        PROPERTY( Category = "Director", DisplayName = "Director", Tooltip = "Object with the game's director component" )
        GameObjectHandle _gameMode;
    };
} // namespace sw
