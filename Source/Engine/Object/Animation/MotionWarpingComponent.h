/**
 * @file MotionWarpingComponent.h
 * @brief 모션 워핑 — 클립 알림이 정한 창(`MotionWarp` 구간 알림) 동안 루트 모션을 휘어 창 끝에 이름 붙은 목표(자리 · 방향)에 닿게 합니다.
 * @details 언리얼 Motion Warping(Skew Warp)과 같은 자리입니다. 목표는 게임플레이가 이름으로 넣고(`setWarpTarget` — 상호작용의 맞춤 마커, 공격 대상 앞),
 *          창은 클립의 구간 알림이 엽니다(알림 표 `<Notify name="WarpToDoor" handler="MotionWarp" target="Handle" translation="true" rotation="true"/>`).
 *          매 프레임 남은 루트 모션(지금 → 창 끝, 클립의 루트 모션 트랙)과 목표까지 필요한 이동의 차이를 남은 시간에 비례해 나눠 더합니다 — 창 끝 프레임에
 *          비율이 1 이 되어 정확히 목표에 닿습니다. 회전(요)도 같습니다. 창 밖에서는 아무것도 하지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Notify/AnimNotifyListener.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class MotionWarpingComponent;

    /** @brief 이름 붙은 워프 목표 하나입니다(월드 자리 · 요). */
    struct MotionWarpTarget
    {
        hashed_string _name{};
        float3        _position{};
        float32       _yaw{ 0.0f }; ///< 라디안, +Z 가 0 · +X 가 +π/2(`atan2( x, z )`)
    };
} // namespace sw

namespace sw
{
    /** @brief 열린 워프 창 하나입니다. */
    struct MotionWarpWindow
    {
        hashed_string        _target{};
        const IAnimPlayable* _pClip{ nullptr };
        float32              _endTime{ 0.0f }; ///< 클립 시각(초)
        bool                 _bTranslation{ true };
        bool                 _bRotation{ true };
        bool                 _bVertical{ false };
        bool                 _bEnding{ false }; ///< 구간 끝이 울렸다 — 이번 프레임의 루트 모션까지 휜 뒤 닫는다(끝 프레임이 비율 1 을 받는다)
    };
} // namespace sw

namespace sw
{
    /** @class MotionWarpingBinding @brief 애니메이터에 보이는 얼굴입니다(리플렉션 컴포넌트는 기반 하나). */
    class SW_API MotionWarpingBinding final : public IRootMotionModifier
    {
    public:
        explicit MotionWarpingBinding( MotionWarpingComponent& owner );
        void modifyRootMotion( const SkeletalAnimatorComponent& animator, RootMotionFrame& inoutFrame ) override;

    private:
        MotionWarpingComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class MotionWarpingComponent
     * @brief 워프 목표 표와 열린 창을 들고, 같은 오브젝트의 애니메이터에 루트 모션 고치는 쪽으로 붙습니다. 파일 머리말 참고.
     */
    REFLECT( Category = "Animation", DisplayName = "Motion Warping", Tooltip = "Warps root motion inside notify windows so the clip ends at a named target" )
    class SW_API MotionWarpingComponent : public Component
    {
        friend class MotionWarpingBinding;

    public:
        REFLECT_BODY();

        MotionWarpingComponent();
        virtual ~MotionWarpingComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;

        /** @brief 이름의 목표를 넣거나 바꿉니다(아무 때나 — 같은 오브젝트의 틱에서도). */
        void setWarpTarget( const hashed_string& name, const float3& position, float32 yaw );
        /** @brief 월드 변환(+Z 가 앞)에서 목표를 넣습니다. */
        void setWarpTargetFromTransform( const hashed_string& name, const float4x4& worldTransform );
        /** @brief 목표를 뺍니다. */
        void clearWarpTarget( const hashed_string& name );
        /** @brief 이름의 목표입니다. 없으면 nullptr 입니다. */
        const MotionWarpTarget* findWarpTarget( const hashed_string& name ) const;

        /** @brief 창을 엽니다(`MotionWarp` 처리기가 구간 시작에서 부릅니다). 같은 클립의 같은 목표 창이 열려 있으면 바꿉니다. */
        void beginWindow( const MotionWarpWindow& window );
        /** @brief 창을 닫습니다(구간 끝). 이번 프레임의 루트 모션까지는 휘고 닫습니다 — 알림이 루트 모션보다 먼저 처리된다. */
        void endWindow( const hashed_string& target, const IAnimPlayable* pClip );
        /** @brief 열린 창 수입니다. */
        uint32 getOpenWindowCount() const { return static_cast<uint32>( _listWindow.size() ); }

    private:
        /** @brief 이번 프레임의 루트 모션을 열린 창의 목표로 휩니다(게임 스레드). */
        void warpRootMotion( const SkeletalAnimatorComponent& animator, RootMotionFrame& inoutFrame );
        void bindToAnimator();
        void unbindFromAnimator();

        MotionWarpingBinding     _binding;
        vector<MotionWarpTarget> _listTarget;
        vector<MotionWarpWindow> _listWindow;
        ComponentHandle          _animator;
    };
} // namespace sw
