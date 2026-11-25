/**
 * @file GameModeViewComponent.h
 * @brief 같은 오브젝트의 디렉터(`GameModeComponent`)를 보이고 입력을 명령으로 넘기는 클라이언트 컴포넌트의 베이스입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

#include <type_traits>

namespace sw
{
    class GameModeComponent;
    class GameModeViewComponent;
    class GameObject;
    class GameObjectManager;
    class InputManager;

    /**
     * @class GameModeInput
     * @brief 뷰가 명령을 만들 때 읽는 입력입니다 — 액션(`GameInputView`)과, 포인터를 읽을 장치 · 오브젝트 매니저 · 로컬 사용자 번호입니다. 부르는 동안만 유효합니다.
     */
    class GameModeInput final : public GameInputView
    {
    public:
        GameModeInput( const GameInputView& view, const InputManager& devices, GameObjectManager& manager, uint32 localUser )
            : GameInputView( view )
            , _devices{ devices }
            , _manager{ manager }
            , _localUser{ localUser }
        {
        }

        const InputManager& getDevices() const { return _devices; }
        GameObjectManager&  getManager() const { return _manager; }
        uint32              getLocalUser() const { return _localUser; }

    private:
        const InputManager& _devices;
        GameObjectManager&  _manager;
        uint32              _localUser;
    };

    /** @brief 입력 함수를 부르는 사람입니다. */
    enum class GameModeInputUser : uint8
    {
        Primary,  ///< 첫 사람의 입력 맵 한 번
        EachLocal ///< 로컬 사용자마다 그 사람의 입력 맵으로 한 번씩(`GameModeInput::getLocalUser`)
    };

    /**
     * @struct GameModeViewBinding
     * @brief 뷰의 갱신 · 입력 함수와 디렉터 형식을 잇습니다. `of<&뷰::갱신, &뷰::입력>()` 한 줄이 디렉터 형식을 함수 인자에서 정해, 다른 형식의 디렉터로 부를 수 없다.
     * @details 갱신은 `void ( GameObjectManager&, 디렉터& )`, 입력은 `void ( const GameModeInput&, 디렉터& )`(또는 `const GameInputView&`) 다. 디렉터는 const 여도 된다.
     */
    struct GameModeViewBinding
    {
        using FindFunction   = GameModeComponent* (*)( GameObject& );
        using UpdateFunction = void ( * )( GameModeViewComponent&, GameObjectManager&, GameModeComponent& );
        using InputFunction  = void ( * )( GameModeViewComponent&, const GameModeInput&, GameModeComponent& );

        FindFunction      _pFind;
        UpdateFunction    _pUpdate;
        InputFunction     _pInput; ///< 입력을 받지 않는 뷰면 nullptr
        GameModeInputUser _inputUser;

        template <auto Update>
        static constexpr GameModeViewBinding of()
        {
            return GameModeViewBinding{ &findOwned<Update>, &callUpdate<Update>, nullptr, GameModeInputUser::Primary };
        }
        template <auto Update, auto Input>
        static constexpr GameModeViewBinding of( GameModeInputUser inputUser = GameModeInputUser::Primary )
        {
            static_assert( std::is_same_v<std::remove_const_t<typename Traits<decltype( Update )>::GameMode>, std::remove_const_t<typename Traits<decltype( Input )>::GameMode>>,
                           "the update and the input of one view take the same game mode" );
            return GameModeViewBinding{ &findOwned<Update>, &callUpdate<Update>, &callInput<Input>, inputUser };
        }

    private:
        template <typename T>
        struct Traits;
        template <typename TView, typename TArgument, typename TGameMode>
        struct Traits<void ( TView::* )( TArgument&, TGameMode& )>
        {
            using View     = TView;
            using GameMode = TGameMode;
        };
        template <typename TView, typename TArgument, typename TGameMode>
        struct Traits<void ( TView::* )( TArgument&, TGameMode& ) const> : Traits<void ( TView::* )( TArgument&, TGameMode& )>
        {
        };

        /** @brief 한 오브젝트에 디렉터가 여럿일 수 있다 — 함수가 받는 형식의 것을 찾는다(오브젝트 형식은 의존 이름이라 이 헤더가 오브젝트를 include 하지 않는다). */
        template <auto Update, typename TObject = GameObject>
        static GameModeComponent* findOwned( GameObject& object )
        {
            return static_cast<TObject&>( object ).template getComponent<std::remove_const_t<typename Traits<decltype( Update )>::GameMode>>();
        }
        template <auto Update>
        static void callUpdate( GameModeViewComponent& view, GameObjectManager& manager, GameModeComponent& gameMode )
        {
            using T = Traits<decltype( Update )>;
            ( static_cast<typename T::View&>( view ).*Update )( manager, static_cast<typename T::GameMode&>( gameMode ) );
        }
        template <auto Input>
        static void callInput( GameModeViewComponent& view, const GameModeInput& input, GameModeComponent& gameMode )
        {
            using T = Traits<decltype( Input )>;
            ( static_cast<typename T::View&>( view ).*Input )( input, static_cast<typename T::GameMode&>( gameMode ) );
        }
    };

    /**
     * @class GameModeViewComponent
     * @brief 디렉터 하나를 보이는 클라이언트 컴포넌트의 공통 몫입니다 — 디렉터 찾기 · 열린 판에서만 갱신 · 틱 뒤 게임 스레드 · 입력 거르기를 맡습니다.
     * @details 디렉터는 같은 오브젝트에 먼저 붙인다. 등록에서 찾아 핸들로 들고, 없으면 플레이가 시작될 때 오류 하나를 남긴다(편집 중에는 컴포넌트를 하나씩 더하는 동안 비어 있다).
     *          파생은 판이 열린 동안만 불리는 갱신 함수 하나와 입력 함수 하나를 적는다(`GameModeViewBinding`) — 열렸는지 · 시간이 흐르는지 · 메뉴가 입력을 먹었는지를 파생이 다시 묻지 않는다.
     *          `PostUpdate` 라 디렉터 걸음 뒤에 돌아, 넘긴 입력은 다음 걸음이 쓰고 모습은 이번 걸음의 결과를 보인다(언리얼 PlayerController 입력 → 다음 폰 이동과 같은 한 프레임).
     *          디렉터의 명령과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
     */
    REFLECT( Abstract, Category = "Framework", DisplayName = "Game Mode View", Tooltip = "Base of the client components that show the game mode on the same object and send it input commands" )
    class SW_GF_API GameModeViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        virtual ~GameModeViewComponent() override;

        void onRegister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

    protected:
        explicit GameModeViewComponent( const GameModeViewBinding& binding );

        /** @brief 갱신 함수 안에서 입력을 넘길 자리에 부릅니다. 이번 틱의 시간이 흐르고 디렉터가 입력을 받으면(`GameModeComponent::isAcceptingInput`) 입력 함수를 부릅니다. */
        void dispatchInput( GameObjectManager& manager, GameModeComponent& gameMode );
        /** @brief 이번 틱의 시간이 흘렀으면 true 입니다(멈춘 프레임에도 갱신은 온다). */
        bool isTimeRunning() const { return _bTimeRunning == SW_TRUE; }
        /** @brief 이 뷰의 디렉터입니다(판이 열렸는지는 보지 않는다). 없으면 nullptr 입니다. 갱신 함수 밖(감시 값 · 진단)에서 씁니다. */
        const GameModeComponent* findGameMode( const GameObjectManager& manager ) const;

    private:
        /** @brief 틱 뒤 게임 스레드: 판이 열렸으면 갱신 함수를 부릅니다. */
        void updateOpenGame( GameObjectManager& manager );

    private:
        SW_TRANSIENT GameModeViewBinding _binding;
        SW_TRANSIENT TComponentHandle<GameModeComponent> _gameMode;
        SW_TRANSIENT uint8                               _bTimeRunning : 1;
        [[maybe_unused]] uint8                           _reserved     : 7;
    };
} // namespace sw
