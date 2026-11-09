/**
 * @file GrabberComponent.h
 * @brief 집기, 던지기, 붙이기입니다. 물리 백엔드를 모르는 인터페이스(`IGrabPhysics`)와 그것을 쓰는 컴포넌트를 담습니다.
 * @details 게임이 `IGrabPhysics` 를 게임 서비스로 등록하면 그것을 쓰고, 없으면 강체 백엔드(`RigidBodyGrabPhysics`)를 쓴다. 이 백엔드는 강체를 키네마틱으로 손에 묶고, 놓을 때
 *          동적으로 되돌리고 속도를 준다. 강체가 없는 대상은 트랜스폼 폴백(`TransformGrabPhysics`)을 쓴다. 이 폴백은 쥐고 있는 쪽 씬 컴포넌트에 월드 위치를 유지한 채 붙이고, 놓으면 분리한다.
 *          속도를 받을 몸이 없어 놓은 자리에 남는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 집기 백엔드 인터페이스입니다. 게임 스레드의 틱 밖에서 불리며, 컴포넌트가 호출을 틱 뒤로 미룹니다. */
    class IGrabPhysics
    {
    public:
        IGrabPhysics()          = default;
        virtual ~IGrabPhysics() = default;

        IGrabPhysics( const IGrabPhysics& )            = default;
        IGrabPhysics& operator=( const IGrabPhysics& ) = default;

        /** @brief @p target 을 @p holder 의 @p localOffset 위치에 붙입니다. 붙였으면 true 입니다. */
        [[nodiscard]] virtual bool attach( GameObject& holder, GameObject& target, const float3& localOffset ) = 0;
        /** @brief 붙인 것을 풀고 @p velocity(월드, m/s)로 놓습니다. 0 이면 그냥 떨어뜨립니다. */
        virtual void release( GameObject& holder, GameObject& target, const float3& velocity ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 물리 없이 트랜스폼으로 붙이는 폴백입니다. */
    class SW_GF_API TransformGrabPhysics final : public IGrabPhysics
    {
    public:
        [[nodiscard]] bool attach( GameObject& holder, GameObject& target, const float3& localOffset ) override;
        void               release( GameObject& holder, GameObject& target, const float3& velocity ) override;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RigidBodyGrabPhysics
     * @brief 강체를 키네마틱으로 손에 붙이고(트랜스폼을 따르며, 밀린 것이 속도를 받음), 놓을 때 동적으로 되돌려 속도를 줍니다. 3D 강체와 2D 강체를 모두 지원합니다.
     *        강체가 없는 대상은 트랜스폼 폴백(`TransformGrabPhysics`)과 같게 동작합니다. 서비스가 없을 때 `GrabberComponent` 가 쓰는 기본 백엔드입니다.
     */
    class SW_GF_API RigidBodyGrabPhysics final : public IGrabPhysics
    {
    public:
        [[nodiscard]] bool attach( GameObject& holder, GameObject& target, const float3& localOffset ) override;
        void               release( GameObject& holder, GameObject& target, const float3& velocity ) override;

    private:
        TransformGrabPhysics _transform;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GrabberComponent
     * @brief 하나를 집어 들고(`grab`) 던지거나(`throwHeld`) 내려놓습니다(`dropHeld`). 소켓에 붙이기(무기를 등에 메기)는 `grab` 에 그 소켓의 위치를 오프셋으로 줍니다.
     * @details 쥔 대상은 핸들로 저장한다(`_held`). 백엔드는 게임 서비스 `IGrabPhysics` 를 쓰고, 없으면 `RigidBodyGrabPhysics` 를 쓴다.
     *          **입력**: 같은 오브젝트가 폰이면 의도 버튼 `_grabButton` 이 발동할 때 집기 · 놓기를 번갈아 한다(`MountInteractionComponent` 와 같은 길 — 입력 맵은 읽지 않고
     *          폰의 의도만, 일은 틱 뒤). 집을 것은 태그 `_grabbableTags` 를 가진 것 중 `_reach` 안의 가장 가까운 것이고, 놓을 때는 앞(+Z)으로 `_throwSpeed` 만큼 던진다(0 이면 내려놓기).
     *          하프라이프 2 · 유니티 Starter Assets 의 "한 키로 들고 다시 눌러 놓기" 와 같다. 버튼이 비면 입력을 듣지 않는다(코드 · 개발 명령만).
     */
    REFLECT( Category = "Interaction", DisplayName = "Grabber", Tooltip = "Grab, carry, throw or attach one object through the grab physics interface" )
    class SW_GF_API GrabberComponent : public Component
    {
    public:
        REFLECT_BODY();

        GrabberComponent();
        virtual ~GrabberComponent() override = default;

        /** @brief 폰의 의도 버튼 번호를 찾고 PrePhysics 에서 틱합니다(버튼이 있을 때). */
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 집습니다. 이미 쥐고 있거나 자기 자신이면 false 입니다. 게임 스레드에서 부릅니다. */
        [[nodiscard]] bool grab( GameObject& target );
        /** @brief 든 것을 @p velocity 로 던집니다. 든 것이 없으면 false 입니다. */
        bool throwHeld( const float3& velocity );
        /** @brief 든 것을 내려놓습니다. */
        bool dropHeld() { return throwHeld( float3{} ); }
        /** @brief 입력 한 번입니다. 들고 있으면 앞으로 `_throwSpeed` 로 놓고, 아니면 `_reach` 안의 가장 가까운 `_grabbableTags` 대상을 집습니다. 무엇을 했으면 true 입니다. 게임 스레드, 틱 밖. */
        bool toggleGrab();
        /** @brief `_reach` 안에서 `_grabbableTags` 를 가진 가장 가까운 오브젝트입니다(자기 자신 제외). 없으면 nullptr 입니다. */
        GameObject* findNearestGrabbable();

        GameObjectHandle getHeld() const { return _held; }
        bool             isHolding() const { return _held.isValid(); }
        void             setHoldOffset( const float3& offset ) { _holdOffset = offset; }

    private:
        IGrabPhysics& getBackend();

    private:
        PROPERTY( Category = "Grab", DisplayName = "Hold Offset", Tooltip = "Where the held object sits, in this object's space", Units = m )
        float3 _holdOffset;
        PROPERTY( Category = "Grab", DisplayName = "Held", Tooltip = "Object held now (runtime)" )
        GameObjectHandle _held;
        PROPERTY( Category = "Input", DisplayName = "Grab Button", Tooltip = "Pawn intent button that picks up or puts down (empty: no input, code and dev commands only)" )
        hashed_string _grabButton;
        PROPERTY( Category = "Input", DisplayName = "Grabbable Tags", Tooltip = "Only objects with these tags are picked up by the button" )
        TagContainer _grabbableTags;
        PROPERTY( Category = "Input", DisplayName = "Reach", Min = 0.0, Tooltip = "Farthest object the button picks up", Units = m )
        float32 _reach;
        PROPERTY( Category = "Input", DisplayName = "Throw Speed", Min = 0.0, Tooltip = "Forward speed given when the button lets go (0: put down)", Units = "m/s" )
        float32 _throwSpeed;

        RigidBodyGrabPhysics _fallback;
        optional<int32>      _grabIndex;  ///< `_grabButton` 의 폰 버튼 번호(없으면 비어 있다)
        vector<GameObject*>  _listTagged; ///< 후보 버퍼(태그 색인 결과)
    };
} // namespace sw
