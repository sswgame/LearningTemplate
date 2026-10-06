/**
 * @file SocketBindingComponent.h
 * @brief 소켓 부착 컴포넌트 — 애니메이션 단위(부품 GameObject)를 다른 단위의 소켓에 붙이고 떼고(애니메이션 · 물리) 되돌립니다. 2D · 3D 공용입니다.
 * @details 상태는 넷입니다 — 붙음(`Bound`) · 떼어 애니메이션(`ReleasedAnimated`) · 떼어 물리(`ReleasedPhysics`) · 되돌아감(`Returning`).
 *          **붙어 있는 동안 비용이 없습니다**: 주인의 루트 씬 컴포넌트를 들고 있는 쪽(holder)의 루트에 붙이고 소켓 변환을 로컬로 적을 뿐이라,
 *          트랜스폼 계층이 나머지를 합니다. 소켓의 본이 움직이면 부착을 만든 쪽(`CharacterAppearanceComponent`)이 `updateSocketTransform` 을 부릅니다
 *          (바뀌지 않으면 아무 일도 없다). 애니메이션 시스템이 저절로 부르는 경로는 아직 없습니다. 틱은 물리 · 되돌아가기 동안만 켭니다.
 *
 *          전환은 언제나 **지금 월드 변환**에서 출발합니다 — 떼는 순간 월드 자리를 지키고(튀지 않음), 되돌아가기는 그 자리에서 소켓까지 블렌드
 *          곡선(`BlendCurveSpec` — 카메라 블렌드와 같은 구현)으로 섞습니다. 물리는 `ISocketPhysicsBody` 를 구현한 컴포넌트(강체)가 맡고, 이
 *          컴포넌트는 그 인터페이스만 압니다. 주인을 바꾸는 일(땅에 떨어진 무기를 집기 · 다른 캐릭터에게 넘기기)은 다시 스폰하지 않고
 *          `transferTo` 한 번입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/BlendCurve.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObject;
    class ISocketPhysicsBody;
    class SceneComponent;

    /** @brief 소켓 부착의 상태입니다. */
    ENUM()
    enum class SocketBindingState : uint8
    {
        Bound = 0,        ///< 소켓에 붙어 있다 — 트랜스폼 계층이 따라가게 한다(틱 없음)
        ReleasedAnimated, ///< 떼어 냈다 — 자기 애니메이션(또는 게임 코드)이 움직인다(틱 없음)
        ReleasedPhysics,  ///< 떼어 냈다 — 물리 바디가 움직이고 매 틱 그 변환을 읽는다
        Returning,        ///< 지금 자리에서 소켓으로 섞으며 돌아간다 — 끝나면 붙음
    };

    /** @brief 떼는 방식입니다. */
    enum class SocketReleaseMode : uint8
    {
        Animated = 0, ///< `ReleasedAnimated`
        Physics,      ///< `ReleasedPhysics` — 물리 바디가 있어야 한다
    };
} // namespace sw

namespace sw
{
    /**
     * @class SocketBindingComponent
     * @brief 부품 GameObject 하나를 다른 단위의 소켓에 붙이는 컴포넌트입니다. 소켓 변환은 holder 루트 기준(유닛 공간)으로 받습니다.
     * @details 소켓 이름 → 변환은 해석된 외형의 소켓 표(`ResolvedSocketTable`)가 풀고, 부르는 쪽(외형 컴포넌트 · 애니메이션 시스템)이 넘깁니다.
     *          2D 도 같습니다 — 2D 오브젝트도 씬 컴포넌트(X · Y, Z 축 회전)라 같은 계층을 탑니다.
     */
    REFLECT( Category = "Character", DisplayName = "Socket Binding", Tooltip = "Binds this unit to a socket of another unit; release to animation or physics and blend back" )
    class SW_API SocketBindingComponent : public Component
    {
    public:
        REFLECT_BODY();

        SocketBindingComponent();
        virtual ~SocketBindingComponent() override = default;

        /** @brief 물리 · 되돌아가기 동안만 돕니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 물리 바디가 정해지지 않았으면 같은 오브젝트의 강체(`RigidBodyComponent`)를 씁니다. */
        void onBeginPlay() override;
        /** @brief 물리 바디를 쓰고 있으면 끝냅니다. */
        void onEndPlay() override;

        /**
         * @brief @p pHolder 의 소켓에 붙습니다. 물리 중이면 끝내고, 되돌아가는 중이면 멈춥니다.
         * @param socketInHolder holder 루트 기준 소켓 변환(해석된 소켓 표가 holder 의 본으로 계산한 것)입니다.
         * @return 주인 · holder 에 씬 컴포넌트가 없거나 붙일 수 없으면 false 입니다.
         */
        bool bindToSocket( GameObject* pHolder, const hashed_string& socketName, const float4x4& socketInHolder );
        /**
         * @brief 소켓 변환이 바뀌었습니다(holder 의 본이 움직임). 붙어 있으면 로컬을 바꾸고, 되돌아가는 중이면 목표를 바꿉니다. 같은 값이면 아무것도 안 합니다.
         */
        void updateSocketTransform( const float4x4& socketInHolder );
        /**
         * @brief 소켓에서 뗍니다 — 지금 월드 자리를 지킵니다(첫 프레임이 붙어 있던 자리와 같다).
         * @param linearVelocity 물리로 뗄 때 바디의 시작 속도입니다.
         * @return 붙어 있지 않거나(이미 뗐음), 물리인데 바디가 없으면 false 입니다.
         */
        bool release( SocketReleaseMode mode, const float3& linearVelocity = float3::Zero );
        /** @brief 지금 자리에서 기억해 둔 소켓으로 블렌드하며 돌아갑니다(`_returnBlend`). 길이가 0 이면 바로 붙습니다. holder 가 없으면 false 입니다. */
        bool returnToSocket();
        /**
         * @brief 다시 스폰하지 않고 주인을 바꿉니다(땅의 줍기 오브젝트 · 다른 캐릭터). @p bBlend 면 지금 자리에서 새 소켓으로 블렌드합니다.
         * @return 새 holder 에 붙일 수 없으면 false 입니다(상태는 그대로).
         */
        bool transferTo( GameObject* pNewHolder, const hashed_string& socketName, const float4x4& socketInHolder, bool bBlend );

        /**
         * @brief 물리 바디를 정합니다. @p pBodyComponent 는 그 인터페이스를 구현한 같은 단위의 컴포넌트이고, 핸들로 들어 살아 있을 때만 씁니다.
         * @details 강체 컴포넌트가 붙을 때 자기를 넘깁니다(이 컴포넌트는 강체 타입을 모른다). 널이면 지웁니다.
         */
        void setPhysicsBody( Component* pBodyComponent, ISocketPhysicsBody* pBody );

        /** @brief 지금 상태입니다. */
        SocketBindingState getState() const { return _state; }
        /** @brief 붙은(또는 돌아갈) 소켓 이름입니다. */
        const hashed_string& getSocketName() const { return _socketName; }
        /** @brief 소켓을 가진 쪽입니다. */
        const GameObjectHandle& getHolder() const { return _holder; }
        /** @brief 되돌아가기 진행(0..1)입니다. 되돌아가는 중이 아니면 0 입니다. */
        float32 getReturnProgress() const;
        /** @brief 되돌아가기 곡선 · 길이입니다. */
        const BlendCurveSpec& getReturnBlend() const { return _returnBlend; }
        /** @brief 되돌아가기 곡선 · 길이를 정합니다. */
        void setReturnBlend( const BlendCurveSpec& blend ) { _returnBlend = blend; }

    private:
        SceneComponent*     findOwnerScene() const;
        GameObject*         resolveHolder() const;
        ISocketPhysicsBody* resolvePhysicsBody() const;
        bool                computeSocketWorld( float4x4& outWorldTransform ) const;
        [[nodiscard]] bool  attachToHolder();
        void                endPhysicsIfRunning();
        void                enterState( SocketBindingState state );

    private:
        PROPERTY( Category = "Socket", DisplayName = "Return Blend", Tooltip = "Curve and length of the blend from the released transform back to the socket" )
        BlendCurveSpec _returnBlend;
        PROPERTY( Category = "Socket", DisplayName = "State", ReadOnly, Transient )
        SocketBindingState _state;

        float4x4            _socketInHolder;
        float4x4            _returnFrom;
        hashed_string       _socketName;
        GameObjectHandle    _holder;
        ComponentHandle     _physicsBodyComponent;
        ISocketPhysicsBody* _pPhysicsBody;
        float32             _returnElapsed;
    };
} // namespace sw
