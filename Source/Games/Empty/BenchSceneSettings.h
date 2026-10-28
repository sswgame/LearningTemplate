/**
 * @file BenchSceneSettings.h
 * @brief 벤치 씬의 배치 값, 튜닝 값, 에셋 경로입니다. 값의 원본은 `Content/game/empty/data/benchscene.xml` 입니다.
 * @details 어떤 벤치를 켤지(`-gv_bench*`)는 Test/Load 시나리오와 실행기가 명령줄로 넘기는 인터페이스이므로 그대로 두었습니다.
 *          켠 벤치가 **어떻게 배치되는지**를 정하는 값만 이 데이터로 옮겼습니다. 간격, 높이, 카메라 여백, 색, 크기, 에셋 경로, 클립 이름이 해당합니다.
 *          생성자가 없고 필드는 빈 값입니다. 코드에는 기본값이 없으므로 파일을 읽지 못하면 벤치를 시작하지 않습니다.
 *          XML 에서 키를 빠뜨리면 그 필드는 0 이나 빈 문자열이 됩니다. 결정적 스크린샷이 이 값에 묶여 있으므로, 값을 바꾸면 기준 이미지도 바뀝니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct BenchSceneSettings
     * @brief `BenchScene` 이 읽는 리플렉션 설정입니다(`ReflectedXMLFile::loadDesc`).
     */
    REFLECT()
    struct BenchSceneSettings
    {
        REFLECT_BODY();

        // ---- 에셋 ----
        PROPERTY( Category = "Assets", Tooltip = "Skinned knight mesh used by the character and rig benches" )
        string _knightMesh{};
        PROPERTY( Category = "Assets", Tooltip = "Material of the knight (KayKit atlas)" )
        string _knightMaterial{};
        PROPERTY( Category = "Assets" )
        string _knightSkeleton{};
        PROPERTY( Category = "Assets" )
        string _knightClips{};
        PROPERTY( Category = "Assets", Tooltip = "Clip played by even-numbered bench characters" )
        string _characterEvenClip{};
        PROPERTY( Category = "Assets", Tooltip = "Clip played by odd-numbered bench characters" )
        string _characterOddClip{};
        PROPERTY( Category = "Assets" )
        string _faceMesh{};
        PROPERTY( Category = "Assets" )
        string _faceSkeleton{};
        PROPERTY( Category = "Assets" )
        string _faceClips{};
        PROPERTY( Category = "Assets" )
        string _faceRig{};
        PROPERTY( Category = "Assets", Tooltip = "Voice line spoken by odd-numbered heads" )
        string _faceVoice{};
        PROPERTY( Category = "Assets", Tooltip = "Clip played by even-numbered heads" )
        string _faceTalkClip{};
        PROPERTY( Category = "Assets", Tooltip = "Expression worn by even-numbered heads" )
        string _faceExpression{};
        PROPERTY( Category = "Assets", Tooltip = "Material of the transparent cubes" )
        string _glassMaterial{};
        PROPERTY( Category = "Assets", Tooltip = "Animator state of the rig knights" )
        string _rigAimState{};
        PROPERTY( Category = "Assets" )
        string _rigWeaponMesh{};
        PROPERTY( Category = "Assets", Tooltip = "Material of the rig weapon (KayKit atlas)" )
        string _rigWeaponMaterial{};
        PROPERTY( Category = "Assets" )
        string _rigCapeMesh{};
        PROPERTY( Category = "Assets", Tooltip = "Material of the rig cape (KayKit atlas)" )
        string _rigCapeMaterial{};
        PROPERTY( Category = "Assets" )
        string _rigKnightRig{};
        PROPERTY( Category = "Assets" )
        string _rigWeaponRig{};
        PROPERTY( Category = "Assets" )
        string _rigCapeRig{};
        PROPERTY( Category = "Assets" )
        string _rigCapeSkeleton{};
        PROPERTY( Category = "Assets" )
        string _rigWeaponSockets{};
        PROPERTY( Category = "Assets", Tooltip = "Skinned enemy mesh of the combat bench" )
        string _combatEnemyMesh{};
        PROPERTY( Category = "Assets", Tooltip = "Material of the combat enemy (KayKit atlas)" )
        string _combatEnemyMaterial{};
        PROPERTY( Category = "Assets" )
        string _combatEnemySkeleton{};
        PROPERTY( Category = "Assets" )
        string _combatEnemyClips{};
        PROPERTY( Category = "Assets", Tooltip = "Path stem of the enemy's character data (.notifies.xml, .sockets.xml, .physics.xml are appended)" )
        string _combatEnemyData{};
        PROPERTY( Category = "Assets", Tooltip = "Sword mesh held in the enemy's right hand" )
        string _combatSwordMesh{};
        PROPERTY( Category = "Assets", Tooltip = "Material of the sword (KayKit atlas)" )
        string _combatSwordMaterial{};
        PROPERTY( Category = "Characters", Tooltip = "Distance in front of the combat enemy the bench shots are fired from", Min = 0.0, Units = m )
        float32 _combatShooterRange{};

        // ---- 큐브 격자 ----
        PROPERTY( Category = "Cubes", Tooltip = "Distance between cubes", Min = 0.0, Units = m )
        float32 _cubeSpacing{};
        PROPERTY( Category = "Cubes", Tooltip = "Camera height as a fraction of the grid half extent", Min = 0.0, Units = ratio )
        float32 _cameraHeightRatio{};
        PROPERTY( Category = "Cubes", Tooltip = "Seconds after which the cameras are framed once more (the editor camera comes late)", Min = 0.0, Units = s )
        float32 _cameraRefreshDelay{};
        PROPERTY( Category = "Cubes", Tooltip = "Height of the up and down wave", Min = 0.0, Units = m )
        float32 _waveHeight{};
        PROPERTY( Category = "Cubes", Tooltip = "Wave phase added per cube index", Units = rad )
        float32 _wavePhaseStep{};
        PROPERTY( Category = "Cubes", Tooltip = "Cube scale at the middle of the wave", Min = 0.0 )
        float32 _waveScaleMin{};
        PROPERTY( Category = "Cubes", Tooltip = "Cube scale added at the top and bottom of the wave", Min = 0.0 )
        float32 _waveScaleRange{};
        PROPERTY( Category = "Cubes", Tooltip = "Alpha of the first transparent material", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _transparentAlphaBase{};
        PROPERTY( Category = "Cubes", Tooltip = "Alpha added per further transparent material", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _transparentAlphaStep{};
        PROPERTY( Category = "Cubes", Tooltip = "Height of the screen marker above a cube", Units = m )
        float32 _markerHeight{};

        // ---- 라이트 ----
        PROPERTY( Category = "Lights", Tooltip = "Intensity of the bench key light", Min = 0.0 )
        float32 _keyLightIntensity{};
        PROPERTY( Category = "Lights", Tooltip = "Shadow extent as a multiple of the scene half extent", Min = 0.0 )
        float32 _shadowExtentRatio{};
        PROPERTY( Category = "Lights", Tooltip = "Shadow distance as a multiple of the scene half extent", Min = 0.0 )
        float32 _shadowDistanceRatio{};
        PROPERTY( Category = "Lights", Tooltip = "Scattered light radius as a multiple of the cube spacing (when -gv_benchLightRadius is 0)", Min = 0.0 )
        float32 _lightRadiusSpacingRatio{};
        PROPERTY( Category = "Lights", Tooltip = "Lowest height of a scattered light", Units = m )
        float32 _lightMinHeight{};
        PROPERTY( Category = "Lights", Tooltip = "Height range above the lowest height of the scattered lights", Min = 0.0, Units = m )
        float32 _lightHeightRange{};
        PROPERTY( Category = "Lights", Min = 0.0 )
        float32 _pointLightIntensity{};
        PROPERTY( Category = "Lights", Min = 0.0 )
        float32 _spotLightIntensity{};
        PROPERTY( Category = "Lights", Min = 0.0, Units = rad )
        float32 _spotOuterConeAngle{};
        PROPERTY( Category = "Lights", Min = 0.0, Units = rad )
        float32 _spotInnerConeAngle{};

        // ---- 바닥 · 캡처 카메라 ----
        PROPERTY( Category = "Ground", Tooltip = "Ground side length as a multiple of the scene half extent", Min = 0.0 )
        float32 _groundSizeRatio{};
        PROPERTY( Category = "Ground", Tooltip = "Height of the ground plane (below the cube bottoms)", Units = m )
        float32 _groundHeight{};
        PROPERTY( Category = "Ground", Tooltip = "Segments per side of the ground plane", Min = 1 )
        uint32 _groundSegmentCount{};
        PROPERTY( Category = "Views", Tooltip = "Width and height of each capture camera's render texture", Min = 1 )
        uint32 _viewTextureSize{};
        PROPERTY( Category = "Views", Tooltip = "Capture camera circle radius as a multiple of the scene half extent", Min = 0.0 )
        float32 _viewRadiusRatio{};
        PROPERTY( Category = "Views", Tooltip = "Capture camera height as a multiple of the scene half extent", Min = 0.0 )
        float32 _viewHeightRatio{};
        PROPERTY( Category = "Views", Tooltip = "Capture camera far plane as a multiple of the scene half extent", Min = 0.0 )
        float32 _viewFarRatio{};

        // ---- 캐릭터 · 전투 ----
        PROPERTY( Category = "Characters", Tooltip = "Distance between characters in a row or grid", Min = 0.0, Units = m )
        float32 _characterSpacing{};
        PROPERTY( Category = "Characters", Tooltip = "Smallest half extent the character framing uses", Min = 0.0, Units = m )
        float32 _characterMinHalfExtent{};
        PROPERTY( Category = "Characters", Tooltip = "Framing margin on the half extent of a character or knight row", Min = 0.0 )
        float32 _characterFrameMargin{};
        PROPERTY( Category = "Characters", Tooltip = "Camera height in front of a character or knight row", Units = m )
        float32 _characterEyeHeight{};
        PROPERTY( Category = "Characters", Tooltip = "Height the camera looks at (waist)", Units = m )
        float32 _characterLookHeight{};
        PROPERTY( Category = "Characters", Tooltip = "Crowd grid camera height as a multiple of the camera distance", Min = 0.0 )
        float32 _crowdEyeHeightRatio{};
        PROPERTY( Category = "Characters", Tooltip = "Crowd grid camera pull-back as a multiple of the camera distance", Min = 0.0 )
        float32 _crowdBackRatio{};
        PROPERTY( Category = "Characters", Tooltip = "Crowd grid look-at depth as a fraction of the grid depth", Min = 0.0, Units = ratio )
        float32 _crowdLookDepthRatio{};
        PROPERTY( Category = "Characters", Tooltip = "Widest start time spread with -gv_benchCharacterStagger", Min = 0.0, Units = s )
        float32 _characterStaggerSeconds{};
        PROPERTY( Category = "Characters", Tooltip = "Half extent the combat bench lights and frames", Min = 0.0, Units = m )
        float32 _combatHalfExtent{};

        // ---- 얼굴 ----
        PROPERTY( Category = "Faces", Tooltip = "Distance between heads", Min = 0.0, Units = m )
        float32 _faceSpacing{};
        PROPERTY( Category = "Faces", Tooltip = "Height of the head centre", Units = m )
        float32 _faceHeight{};
        PROPERTY( Category = "Faces", Tooltip = "Weight of the expression on even-numbered heads", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _faceExpressionWeight{};
        PROPERTY( Category = "Faces", Tooltip = "Half extent the face bench lights", Min = 0.0, Units = m )
        float32 _faceLightHalfExtent{};
        PROPERTY( Category = "Faces", Tooltip = "Closest camera distance to the head row", Min = 0.0, Units = m )
        float32 _faceCameraMinDistance{};
        PROPERTY( Category = "Faces", Tooltip = "Camera distance as a multiple of the head row width", Min = 0.0 )
        float32 _faceCameraWidthMargin{};

        // ---- 리그 ----
        PROPERTY( Category = "Rig", Tooltip = "Distance between rig knights", Min = 0.0, Units = m )
        float32 _rigSpacing{};
        PROPERTY( Category = "Rig", Tooltip = "Slope tilt around Z (+X is higher), so the two feet stand at different heights", Units = rad )
        float32 _rigSlopeAngle{};
        PROPERTY( Category = "Rig", Tooltip = "Smallest half width of the slope", Min = 0.0, Units = m )
        float32 _rigSlopeMinHalfWidth{};
        PROPERTY( Category = "Rig", Tooltip = "Slope added past the outer knights", Min = 0.0, Units = m )
        float32 _rigSlopeEndMargin{};
        PROPERTY( Category = "Rig", Units = m )
        float3 _rigSlopeCenter{};
        PROPERTY( Category = "Rig", Min = 0.0, Units = m )
        float32 _rigSlopeHalfThickness{};
        PROPERTY( Category = "Rig", Min = 0.0, Units = m )
        float32 _rigSlopeHalfDepth{};
        PROPERTY( Category = "Rig", Tooltip = "Scale of the look target sphere", Min = 0.0 )
        float32 _rigLookTargetScale{};
        PROPERTY( Category = "Rig", Tooltip = "Look target position before the first update", Units = m )
        float3 _rigLookTargetStart{};
        PROPERTY( Category = "Rig", Tooltip = "Sideways reach of the look target's swing", Min = 0.0, Units = m )
        float32 _rigLookSwing{};
        PROPERTY( Category = "Rig", Tooltip = "Rate of the look target's sideways swing", Units = "rad/s" )
        float32 _rigLookSwingRate{};
        PROPERTY( Category = "Rig", Tooltip = "Middle height of the look target", Units = m )
        float32 _rigLookHeight{};
        PROPERTY( Category = "Rig", Tooltip = "Up and down reach of the look target", Min = 0.0, Units = m )
        float32 _rigLookBob{};
        PROPERTY( Category = "Rig", Tooltip = "Rate of the look target's up and down motion", Units = "rad/s" )
        float32 _rigLookBobRate{};
        PROPERTY( Category = "Rig", Tooltip = "Z of the look target while it moves", Units = m )
        float32 _rigLookDepth{};
        PROPERTY( Category = "Rig", Tooltip = "Time the frozen picture (-gv_benchAnimate=0) shows", Min = 0.0, Units = s )
        float32 _rigStillTime{};
        PROPERTY( Category = "Rig", Tooltip = "How far the knights turn left and right so the cape swings", Min = 0.0, Units = rad )
        float32 _rigSwayAngle{};
        PROPERTY( Category = "Rig", Units = "rad/s" )
        float32 _rigSwayRate{};
        PROPERTY( Category = "Rig", Tooltip = "Smallest half extent the rig key light covers", Min = 0.0, Units = m )
        float32 _rigMinHalfExtent{};
        PROPERTY( Category = "Rig", Tooltip = "Single-knight camera, -gv_benchRigView=0 (front left: gaze, left hand, feet)", Units = m )
        float3 _rigViewFrontLeft{};
        PROPERTY( Category = "Rig", Tooltip = "Single-knight camera, -gv_benchRigView=1 (front)", Units = m )
        float3 _rigViewFront{};
        PROPERTY( Category = "Rig", Tooltip = "Single-knight camera, -gv_benchRigView=2 (right side: crossbow, cape)", Units = m )
        float3 _rigViewRightSide{};
        PROPERTY( Category = "Rig", Tooltip = "Single-knight camera, -gv_benchRigView=3 (back right: cape, feet)", Units = m )
        float3 _rigViewBackRight{};
        PROPERTY( Category = "Rig", Tooltip = "Point the single-knight camera looks at", Units = m )
        float3 _rigViewFocus{};

        // ---- UI 감시 값 ----
        PROPERTY( Category = "UI", Tooltip = "Nameplate width", Min = 0.0 )
        float32 _nameplateWidth{};
        PROPERTY( Category = "UI", Min = 0.0 )
        float32 _nameplateTextHeight{};
        PROPERTY( Category = "UI", Min = 0.0 )
        float32 _nameplateBarHeight{};
        PROPERTY( Category = "UI", Tooltip = "Gap between the name and the health bar", Min = 0.0 )
        float32 _nameplateSpacing{};
        PROPERTY( Category = "UI", Tooltip = "Height of the nameplate above a cube", Units = m )
        float32 _nameplateHeight{};
        PROPERTY( Category = "UI", Meta = "Color" )
        float4 _nameplateFillColor{};
        PROPERTY( Category = "UI", Meta = "Color" )
        float4 _nameplateBackgroundColor{};
        PROPERTY( Category = "UI", Tooltip = "Width of the Hangul probe text (its height follows the wrapped lines)", Min = 0.0 )
        float32 _hangulTextWidth{};
    };
} // namespace sw
