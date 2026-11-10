/**
 * @file PoseAfterimageComponent.h
 * @brief 캐릭터 잔상 — 같은 오브젝트의 스켈레탈 메시 포즈를 일정 간격으로 고정해 반투명으로 그리고 사라지게 합니다(대시 · 순간이동 · 분신 연출의 잔상).
 * @details VFX 입자에 본 스냅샷을 싣지 않는다 — 스켈레탈 렌더링이 이미 가진 것(스켈레톤 · 스킨 원본 메시 · 고정 포즈 `applyRewindPose`)으로 "포즈를 멈춘 사본" 을 그린다.
 *          사본은 고정 용량 풀(`_maxGhosts`)의 오브젝트이고, 애니메이터가 없어 포즈가 움직이지 않는다(쉬는 유닛은 평가 비용이 없다). 원본의 애니메이션 · 물리 상태는 복제하지 않는다.
 *          투명도는 수명 비율 곡선(`_opacityOverLife`) × 색 알파이고 인스턴스 값(`MeshComponent::setCustomData` 0 번)으로 머티리얼에 간다 — 머티리얼은 그 값을 색으로 쓰는
 *          반투명 머티리얼이어야 한다(엔진 `instancecustomcolor.material`). 언리얼은 포즈 스냅샷(`SnapshotPose`) + Poseable Mesh, 유니티는 `SkinnedMeshRenderer.BakeMesh` 로 같은 일을 한다.
 *          비용 상한: 사본 수 × 그리기 호출 1 · 스킨 팔레트 1. 캡처는 간격마다 한 번(포즈 사본 + 팔레트 계산). 사본은 그림자를 내지 않게 머티리얼이 반투명이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Curve/FloatCurve.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SkeletalMeshComponent;

    /** @class PoseAfterimageComponent @brief 스켈레탈 메시 잔상입니다. 파일 머리말 참고. */
    REFLECT( Category = "Animation", DisplayName = "Pose Afterimage", Tooltip = "Leaves fading frozen copies of this object's skeletal mesh pose (dash trails, clones)", Meta = "Icon=Skeleton" )
    class SW_API PoseAfterimageComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 사본 수의 절대 상한입니다(데이터가 이보다 크면 읽기 범위 오류). 투명 그리기 호출을 묶는다. */
        static constexpr uint32 kMaxGhostLimit = 16;

        PoseAfterimageComponent();
        virtual ~PoseAfterimageComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 잔상 남기기를 켜고 끕니다(능력 · 대시가 부른다). 끄면 남은 사본은 수명대로 사라진다. 켜는 순간 바로 하나를 남긴다. */
        FUNCTION( Category = "Afterimage", CallInEditor )
        void startCapturing();
        FUNCTION( Category = "Afterimage", CallInEditor )
        void stopCapturing();
        bool isCapturing() const { return _bCapturing == SW_TRUE; }

        /** @brief 지금 보이는 사본 수입니다(감시 값). */
        uint32 countVisibleGhosts() const;
        /** @brief 지금까지 남긴 잔상 수입니다(감시 값 — 풀을 다시 쓴 것 포함). */
        uint32 getCaptureCount() const { return _captureCount; }
        /** @brief 수명 비율 @p ageRatio(0..1)의 투명도입니다 — 곡선 × @p baseAlpha, 0..1 로 자른다. 곡선에 키가 없으면 0 입니다(읽기가 막는다). */
        static float32 computeOpacity( const FloatCurve& opacityOverLife, float32 ageRatio, float32 baseAlpha );

    private:
        /** @brief 사본 하나입니다. */
        struct Ghost
        {
            GameObjectHandle _object{};
            float32          _age{ 0.0f };
            bool             _bVisible{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        /** @brief 지금 포즈를 다음 사본(빈 것, 없으면 가장 오래된 것)에 고정합니다. */
        void capture();
        /** @brief 사본 @p ghost 의 스켈레탈 메시입니다(풀은 시작할 때 만든다). 사본 오브젝트가 사라졌으면 nullptr 입니다. */
        SkeletalMeshComponent* findGhostMesh( Ghost& ghost );
        SkeletalMeshComponent* findSourceMesh() const;

    private:
        PROPERTY( Category = "Afterimage", DisplayName = "Material", AssetPath, AssetType = "Material", Required, Tooltip = "Translucent material that reads instance custom data 0 as its colour" )
        string _materialPath;
        PROPERTY( Category = "Afterimage", DisplayName = "Opacity Over Life", Tooltip = "Opacity by life ratio (0 new, 1 gone); multiplied by the colour alpha" )
        FloatCurve _opacityOverLife;
        PROPERTY( Category = "Afterimage", DisplayName = "Color", Tooltip = "Tint and starting alpha" )
        float4 _color;
        PROPERTY( Category = "Afterimage", DisplayName = "Capture Period", Min = 0.01, Units = s, Tooltip = "Seconds between afterimages while capturing" )
        float32 _capturePeriod;
        PROPERTY( Category = "Afterimage", DisplayName = "Lifetime", Min = 0.05, Units = s, Tooltip = "Seconds an afterimage takes to fade out" )
        float32 _lifetime;
        PROPERTY( Category = "Afterimage", DisplayName = "Max Ghosts", Min = 1, Max = 16, Tooltip = "Afterimages alive at once (pool size; the oldest is reused)" )
        uint32 _maxGhosts;
        PROPERTY( Category = "Afterimage", DisplayName = "Capture On Begin Play", Tooltip = "Start capturing when play begins" )
        uint8 _bCaptureOnBeginPlay : 1;

        uint8                  _bCapturing : 1;
        [[maybe_unused]] uint8 _reserved   : 6;
        vector<Ghost>          _listGhost;
        float32                _sinceCapture;
        uint32                 _captureCount;
    };
} // namespace sw
