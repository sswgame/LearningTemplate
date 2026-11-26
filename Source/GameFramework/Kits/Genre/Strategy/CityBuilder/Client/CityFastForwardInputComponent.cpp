#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    class CityGameModeComponent;
    class GameObjectManager;

    /**
     * @class CityFastForwardInputComponent
     * @brief 빨리 감기 액션 하나를 디렉터 명령(`submitFastForward`)으로 바꿉니다. 멈춘 프레임에는 넘기지 않습니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "City Fast Forward Input", Tooltip = "Sends the fast forward input to the City Game Mode on the same object" )
    class CityFastForwardInputComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        CityFastForwardInputComponent();
        virtual ~CityFastForwardInputComponent() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 빨리 감기 입력을 넘깁니다. */
        void updateInput( GameObjectManager& manager, CityGameModeComponent& director );
        void submitInput( const GameModeInput& input, CityGameModeComponent& director );

    private:
        PROPERTY( Required, Category = "Input", DisplayName = "Fast Forward Action", Tooltip = "InputMap action held to fast-forward the city (required)" )
        hashed_string _fastForwardAction;
    };

    CityFastForwardInputComponent::CityFastForwardInputComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&CityFastForwardInputComponent::updateInput, &CityFastForwardInputComponent::submitInput>() )
        , _fastForwardAction{}
    {
        setCanEverTick( true );
    }

    CityFastForwardInputComponent::~CityFastForwardInputComponent() = default;

    void CityFastForwardInputComponent::updateInput( GameObjectManager& manager, CityGameModeComponent& director )
    {
        dispatchInput( manager, director );
    }

    void CityFastForwardInputComponent::submitInput( const GameModeInput& input, CityGameModeComponent& director )
    {
        (void)director.submitFastForward( input.isActionDown( _fastForwardAction ) ); // 열린 판인지는 베이스가 보았다
    }
} // namespace sw

#include "CityFastForwardInputComponent.cpp.gen.inl"
