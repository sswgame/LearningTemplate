/**
 * @file CameraDirector.h
 * @brief 활성 프리셋 · 블렌드 · 감쇠를 시간으로 굴려 매 프레임의 카메라 포즈를 내는 순수 상태 기계입니다(컴포넌트 · 씬을 모른다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Camera/CameraBlend.h"
#include "GameFramework/Camera/CameraPose.h"
#include "GameFramework/Camera/CameraPreset.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class CameraDirector
     * @brief 프리셋을 켜고(`activatePreset`) 시간을 흘리면(`step`) 블렌드 · 감쇠를 거친 포즈를 줍니다.
     * @details 블렌드는 **지금 내보내는 포즈**에서 출발합니다(Cinemachine · 언리얼 PlayerCameraManager 와 같다). 블렌드가 없던 때 켜면 나가는 프리셋도
     *          대상을 계속 따라가며(살아 있는 쪽) 섞이고, 블렌드 도중에 다시 켜면 그 순간의 섞인 포즈를 고정해 거기서 출발합니다 — 어느 쪽이든 켠 순간
     *          화면이 튀지 않습니다. 감쇠는 프리셋마다의 포즈에 걸리고(나가는 쪽 · 들어오는 쪽 각자), 새로 켠 프리셋은 감쇠 없이 그 자리에서 시작합니다.
     *          처음 켠 프리셋(아직 낸 포즈가 없다)은 블렌드 없이 바로 붙습니다.
     */
    class SW_GF_API CameraDirector
    {
    public:
        CameraDirector();

        /** @brief 카탈로그의 프리셋을 켭니다. 블렌드는 `catalog.getBlend( 지금 프리셋, id )` 입니다. 없는 id 면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id );
        /** @brief 카탈로그의 프리셋을 @p blend 로 켭니다(표를 무시한다). */
        [[nodiscard]] bool activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id, const BlendCurveSpec& blend );
        /** @brief 정의를 그대로 켭니다(카탈로그 밖 — 시퀀서 · 코드로 지은 프리셋). */
        void activatePreset( const CameraPresetDef& def, const BlendCurveSpec& blend );

        /** @brief 시간을 흘리고 이번 프레임의 포즈를 돌려줍니다. 켠 프리셋이 없으면 지난 포즈(처음엔 기본값) 그대로입니다. */
        const CameraPose& step( float32 deltaTime, const CameraTarget& target );

        const hashed_string& getActivePresetId() const { return _activeDef._id; }
        bool                 hasActivePreset() const { return _bHasActive == SW_TRUE; }
        bool                 isBlending() const { return _bBlending == SW_TRUE; }
        /** @brief 지금 블렌드의 가중치(0 = 나가는 쪽, 1 = 들어오는 쪽)입니다. 블렌드 중이 아니면 1 입니다. */
        float32           getBlendWeight() const { return _blendWeight; }
        const CameraPose& getPose() const { return _outputPose; }
        bool              hasPose() const { return _bHasPose == SW_TRUE; }

    private:
        CameraPresetDef _activeDef;
        CameraPresetDef _fromDef; ///< 살아 있는 나가는 쪽(`_bFromLive`)일 때만 쓴다
        BlendCurveSpec  _blend;
        CameraPose      _activePose; ///< 들어오는 쪽 — 감쇠를 거친 포즈
        CameraPose      _fromPose;   ///< 나가는 쪽 — 살아 있으면 감쇠를 거친 포즈, 아니면 켠 순간에 고정한 포즈
        CameraPose      _outputPose;
        float32         _blendElapsed;
        float32         _blendWeight;
        uint8           _bHasActive       : 1;
        uint8           _bActivePoseFresh : 1; ///< 다음 `step` 이 감쇠 없이 그 자리에서 시작한다
        uint8           _bBlending        : 1;
        uint8           _bFromLive        : 1;
        uint8           _bHasPose         : 1;
    };
} // namespace sw
