/**
 * @file SinkingSandZoneComponent.h
 * @brief 모래 함몰 구역(사구 · 개미지옥)입니다. 고정 경계 안에 선 캐릭터가 천천히 빠지고, 빠진 만큼 느려지며, 뛰면 빠져나오고, 끝까지 빠지면 삼켜집니다.
 * @details 높이장 · 충돌을 파내지 않는다 — 캐릭터마다 작은 상태(깊이 · 바닥 시간 · 탈출 수 · 단계)만 든다. 모래 표면 자체(보행 감속)는 표면 종류 `Sand` 의 이동 계수가
 *          맡고, 함몰 · 탈출 · 실패는 이 구역만 한다 — 단순한 모래에 함몰 상태를 강제하지 않도록 두 모드를 나눴다.
 *          - **경계**: 오너 월드 자리를 가운데로 한 축 정렬 상자(`_halfExtents`, 회전 · 배율은 보지 않는다 — 지형과 같은 규칙). 발(캐릭터 컨트롤러 자리)이 안이면 점유자다.
 *          - **같은 상태로 움직임과 표현**: 깊이 하나가 이동 계수(`CharacterControllerComponent::applyMoveScale`)와 표현(점유자의 첫 메시 컴포넌트를 깊이만큼 내림)을
 *            같은 틱에 정한다. 들어갈 때 메시의 원래 자리를 적어 두고 나가거나 구역이 끝날 때 되돌린다(겹쳐 쌓이지 않는다).
 *          - **탈출**: 디딘 상태에서 떠오르는 순간(점프)마다 `_escapePerJump` 만큼 깊이가 준다. **실패**: 가장 깊은 곳에 `_engulfSeconds` 머물면 `Engulfed` 다(게임 규칙이 감시 값 · 단계로 읽는다).
 *          - **시간**: 틱의 게임 초(일시정지 · 시간 배율과 같다). 물리 자리 읽기와 메시 쓰기는 틱 뒤 게임 스레드에서 한다.
 *          shortcut: 캡슐은 지면에 남고 메시만 내려간다(Jolt 캐릭터는 정적 바닥 아래로 갈 수 없다) — 빠진 캐릭터를 쏘거나 베는 판정이 깊이를 따라야 하면 캡슐 반 높이를 깊이 단계로 줄인다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Rendering/MeshComponent.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 함몰 구역 점유자의 단계입니다(이름은 `getEnumName` — 감시 값 `SinkingSand.Phase`). */
    ENUM()
    enum class SinkingSandPhase : uint8
    {
        Sinking = 0, ///< 빠지는 중(뛰면 올라온다)
        Engulfed     ///< 가장 깊은 곳에 오래 머물러 삼켜졌다 — 실패 조건
    };

    /**
     * @class SinkingSandZoneComponent
     * @brief 모래 함몰 구역입니다(파일 머리말 참고).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Sinking Sand Zone", Tooltip = "Characters inside the box sink, slow down, climb out by jumping, and are engulfed at full depth" )
    class SW_GF_API SinkingSandZoneComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 점유자 하나의 상태입니다. */
        /** @brief 깊이만큼 내리는 메시 하나와 들어올 때의 로컬 자리입니다. */
        struct SunkVisual
        {
            TComponentHandle<MeshComponent> _mesh;
            float3                          _localPosition;
        };

        struct OccupantRecord
        {
            GameObjectHandle   _object;
            vector<SunkVisual> _listVisual;      ///< 깊이만큼 내리는 메시들 — 다른 메시에 매달리지 않은 메시 전부(몸 · 모자 같은 외형 부품). 비면 이동만 느려진다
            float32            _depth;           ///< 빠진 깊이(m, 0.._maxDepth)
            float32            _secondsAtBottom; ///< 가장 깊은 곳에 머문 게임 초
            uint32             _escapeCount;     ///< 뛰어서 깊이를 줄인 수
            SinkingSandPhase   _phase;
            uint8              _bWasGrounded : 1; ///< 지난 틱에 디디고 있었다(떠오르는 순간을 점프로 본다)
            uint8              _reserved     : 7;

            OccupantRecord();
        };

        SinkingSandZoneComponent();
        virtual ~SinkingSandZoneComponent() override = default;

        void onTick( float32 deltaTime ) override;
        /** @brief 점유자의 메시를 원래 자리로 되돌리고 상태를 버립니다(씬 전환 · 구역 제거에서 표현이 지하에 남지 않게). */
        void onEndPlay() override;

        /** @brief 오브젝트 @p object 의 상태입니다. 점유자가 아니면 nullptr 입니다. */
        const OccupantRecord* findOccupant( GameObjectHandle object ) const;
        /** @brief 깊이 @p depth 에서의 이동 계수입니다(1 → `_moveScaleAtMaxDepth` 를 깊이 비율로 잇는다). */
        float32 computeMoveScale( float32 depth ) const;
        /** @brief 씬에서 @p object 를 점유자로 가진 구역의 상태입니다. 없으면 nullptr 입니다(감시 값 · 진단). */
        static const OccupantRecord* findInScene( const GameObjectManager& manager, GameObjectHandle object );

    private:
        /** @brief 쌓인 게임 초만큼 점유자를 고르고 깊이 · 이동 계수 · 표현을 갱신합니다(틱 뒤 게임 스레드). */
        void updateOccupants( GameObjectManager& manager );
        /** @brief 점유자의 메시를 원래 자리로 되돌립니다. */
        /** @brief @p component 위로 메시가 있는지 봅니다(있으면 그 메시를 따라 내려간다). */
        [[nodiscard]] static bool hasMeshAncestor( const SceneComponent& component );
        static void               restoreVisual( GameObjectManager& manager, const OccupantRecord& record );

    private:
        PROPERTY( Category = "Zone", DisplayName = "Half Extents", Units = m, Tooltip = "Half size of the box around the owner (rotation and scale are not used)" )
        float3 _halfExtents;
        PROPERTY( Category = "Sinking", DisplayName = "Sink Speed", Min = 0.0, Units = "m/s", Tooltip = "How fast an occupant sinks" )
        float32 _sinkSpeed;
        PROPERTY( Category = "Sinking", DisplayName = "Max Depth", Min = 0.01, Units = m, Tooltip = "Deepest an occupant sinks" )
        float32 _maxDepth;
        PROPERTY( Category = "Sinking", DisplayName = "Move Scale At Max Depth", Min = 0.0, Max = 1.0, Tooltip = "Walk speed multiplier at the deepest point (1 at the surface)" )
        float32 _moveScaleAtMaxDepth;
        PROPERTY( Category = "Escape", DisplayName = "Escape Per Jump", Min = 0.0, Units = m, Tooltip = "Depth regained by each jump" )
        float32 _escapePerJump;
        PROPERTY( Category = "Escape", DisplayName = "Engulf Seconds", Min = 0.0, Units = s, Tooltip = "Seconds at the deepest point before the occupant is engulfed" )
        float32 _engulfSeconds;

        vector<OccupantRecord> _listOccupant;
        float32                _pendingSeconds; ///< 틱이 쌓고 틱 뒤 갱신이 쓰는 게임 초
    };
} // namespace sw
