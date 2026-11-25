/**
 * @file GameFramework/Client/HUD/MinimapComponent.h
 * @brief 미니맵입니다. 같은 오브젝트의 HUD 문서에 있는 캔버스 패널에 둘레의 표시(`MinimapMarkerComponent`)를 점으로 놓습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct PossessionChangedEvent;

    class CanvasPanel;
    class GameObject;
    class GameObjectManager;
    class MinimapMarkerComponent;

    /** @brief 표시 하나가 미니맵에 놓일 자리입니다(패널 가운데 기준, 오른쪽 · 아래가 +). */
    struct MinimapPlacement
    {
        float2 _offset{};
        uint8  _bInRange{ SW_TRUE }; ///< 범위 안이다(밖이면 `_offset` 은 가장자리 위의 점)
    };

    /**
     * @class MinimapComponent
     * @brief HUD 문서(`HUDControllerComponent`)의 패널 `_panelName` 에 반지름 `_range`(m) 안의 표시를 원으로 놓습니다. 위는 월드 +Z 또는 보는 쪽(`_bRotateWithView`)입니다.
     * @details 점은 패널의 자식 위젯이고 자리는 렌더 변환만 바꾼다(배치를 다시 하지 않는다). 위젯은 표시 수만큼 한 번 만들고 남는 것은 숨긴다. 문서를 다시 읽어 패널이 새로 생기면 다시 만든다.
     *          그림 지도는 캡처 카메라(`_captureCamera`)가 찍는다 — 점과 같은 가운데 · 범위 · 방향으로 위에서 내려다보게 맞춰 주므로 그 렌더 텍스처를 HUD 문서가 패널 뒤의
     *          `ImageWidget` 에 같은 자리로 놓으면 점과 그림이 겹친다(언리얼 SceneCapture2D, 유니티 `Camera.targetTexture` 미니맵). 카메라가 없으면 점만 있는 레이더다.
     *          가운데와 방향은 이 오브젝트가 아니라 **지금 조종하는 폰**이다 — 플레이어가 다른 폰(말 · 드론 · 탈것)에 빙의하면(`PossessionChangedEvent`) 그 폰을 따라간다. 놓으면 이 오브젝트로 돌아온다.
     */
    REFLECT( TickMode = Main, Category = "UI", DisplayName = "Minimap", Tooltip = "Places the minimap markers around this object as dots on a canvas panel of the HUD document" )
    class SW_GF_API MinimapComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostPhysics; ///< 몸이 이번 프레임에 간 자리

        MinimapComponent();
        ~MinimapComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 표시 @p target(월드)이 놓일 자리입니다. @p yaw 는 위로 둘 방향(라디안, 0 = +Z), @p halfSize 는 패널의 반 크기입니다. */
        [[nodiscard]] static MinimapPlacement computePlacement( const float3& center, float32 yaw, float32 range, const float2& halfSize, const float3& target );
        /** @brief 지금 보는 폰의 자리 · 방향에서 @p marker 가 놓일 자리입니다(패널 반 크기의 비율). 위치가 없으면 범위 밖입니다. */
        [[nodiscard]] MinimapPlacement computeMarkerPlacement( const MinimapMarkerComponent& marker ) const;
        /** @brief HUD 문서의 미니맵 패널입니다. 화면이 없으면 nullptr 입니다. */
        CanvasPanel* findPanel() const;

    private:
        /** @brief 빙의가 바뀌었습니다 — 따라가던 폰에서 옮겨 탄 폰을 따라갑니다(다른 플레이어의 빙의는 보지 않는다). */
        void onPossessionChanged( const PossessionChangedEvent& event );
        /** @brief 가운데로 삼는 오브젝트입니다 — 조종하는 폰, 없거나 사라졌으면 이 오브젝트. */
        const GameObject* findFollowTarget() const;
        /** @brief 위로 둘 방향(라디안, 0 = +Z)입니다. */
        [[nodiscard]] float32 computeYaw() const;
        /** @brief 캡처 카메라를 이 오브젝트 위에서 내려다보게 놓습니다(높이는 카메라의 것). */
        void placeCaptureCamera( GameObjectManager& manager ) const;

    private:
        PROPERTY( Category = "Minimap", DisplayName = "Panel", Tooltip = "Name of the canvas panel in the HUD document" )
        hashed_string _panelName;
        PROPERTY( Category = "Minimap", DisplayName = "Range", Min = 1.0, Tooltip = "World radius shown", Units = m )
        float32 _range;
        PROPERTY( Category = "Minimap", DisplayName = "Rotate With View", Tooltip = "Up is the pawn's control yaw; off: up is world +Z" )
        bool _bRotateWithView;
        PROPERTY( Category = "Minimap", DisplayName = "Capture Camera", Tooltip = "Object with a capture camera that paints the map picture; kept above this object, its height is authored on the camera" )
        GameObjectHandle              _captureCamera;
        SW_TRANSIENT GameObjectHandle _followTarget; ///< 빙의를 따라가 지금 보는 폰(무효면 이 오브젝트)
        Subscription                  _possessionSubscription;
        SW_TRANSIENT vector<uint64> _listDotMarker; ///< 점(패널의 자식 차례)마다 마지막으로 보인 표시의 컴포넌트 id — 바뀐 때만 크기 · 색을 다시 쓴다
    };
} // namespace sw
