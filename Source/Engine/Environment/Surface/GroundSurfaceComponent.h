/**
 * @file GroundSurfaceComponent.h
 * @brief 캐릭터가 디딘 표면을 프레임마다 한 번 읽어(`SurfaceSample`) 그 종류의 이동 계수를 같은 오브젝트의 캐릭터 컨트롤러에 넘깁니다.
 * @details 눈 · 진흙 · 물에서 걷는 속도가 줄어드는 길이다(`CharacterControllerComponent::applyMoveScale`). 읽은 표면(종류 · 형식 · 법선 · 표면 속도 · 물 깊이)은
 *          진단(`surface.probe` · 감시 값 `Surface.GroundKind`)이 그대로 보여 준다 — 이동 디버거가 "왜 느린가" 를 표면 질의와 같은 값으로 설명한다.
 *          질의는 틱 뒤 게임 스레드에서 한다(물리 질의). 표면을 읽기만 하고 높이장 · 충돌을 고치지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Environment/Surface/SurfaceSample.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class GroundSurfaceComponent
     * @brief 디딘 표면의 종류를 읽고 이동 계수를 컨트롤러에 넘깁니다(파일 머리말 참고).
     */
    REFLECT( Category = "Environment", DisplayName = "Ground Surface", Tooltip = "Reads the surface under the character each frame and slows the character controller by its move scale" )
    class SW_API GroundSurfaceComponent : public Component
    {
    public:
        REFLECT_BODY();

        GroundSurfaceComponent();
        virtual ~GroundSurfaceComponent() override = default;

        void onTick( float32 deltaTime ) override;

        /** @brief 마지막으로 읽은 표면 종류 이름입니다(디딘 것이 없으면 비어 있다). */
        const hashed_string& getGroundKind() const { return _groundKind; }
        /** @brief 마지막으로 읽은 표면입니다. `_pKind` 는 읽은 그 프레임에만 유효하다(표가 다시 읽힐 수 있다) — 종류는 `getGroundKind` 로 본다. */
        const SurfaceSample& getLastSample() const { return _lastSample; }

    private:
        /** @brief 발 아래를 읽고 이동 계수를 넘깁니다(틱 뒤 게임 스레드). */
        void sampleGround( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Surface", DisplayName = "Probe Length", Min = 0.05, Units = m, Tooltip = "How far below the feet the ground is looked for" )
        float32 _probeLength;

        SurfaceSample _lastSample;
        hashed_string _groundKind;
    };
} // namespace sw
