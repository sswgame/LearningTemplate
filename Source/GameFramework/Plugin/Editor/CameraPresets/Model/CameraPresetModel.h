/**
 * @file CameraPresetModel.h
 * @brief 카메라 프리셋 창의 모델 — 프리셋 파일(카탈로그), 고른 프리셋 · 대상 요, 미리보기 씬의 대상 캡슐, 고른 프리셋을 풀어 낸 뷰포트 카메라 자리. ImGui 를 모릅니다.
 * @details 패널(`CameraPresetPanel`)은 미리보기 씬 · 뷰포트를 쥐고(`EditorPreviewPanel`) 목록 · 슬라이더를 그리며, 모델이 낸 자리에 뷰포트 카메라를 놓기만 한다 —
 *          에디터 VFX · 메시 미리보기 `Model/` 과 같은 선이다(Cinemachine 의 프리셋 데이터와 Scene 뷰 미리보기가 나뉜 것과 같다). 씬은 패널이 넘긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "GameFramework/Base/Character/Camera/CameraPreset.h"

namespace sw
{
    class Scene;
} // namespace sw

namespace sw::editor
{
    /** @brief 뷰포트 자유 카메라에 놓을 자리입니다(뷰포트 요 · 피치 규약 — 시선 = (-sin 요 cos 피치, -sin 피치, -cos 요 cos 피치)). */
    struct CameraPresetViewportPose
    {
        float3  _position{};
        float32 _yaw{ 0.0f };
        float32 _pitch{ 0.0f };
        float32 _fieldOfViewY{ 0.0f };
    };

    /**
     * @class CameraPresetModel
     * @brief 카메라 프리셋 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class CameraPresetModel
    {
    public:
        CameraPresetModel();

        /** @brief 프리셋 파일 @p assetPath 를 읽고 첫 프리셋을 고릅니다. 다음 `takeViewportPose` 가 카메라를 다시 놓는다. */
        ToolAssetLoadResult loadAsset( string_view assetPath );
        /** @brief 새 미리보기 씬 @p scene 에 대상(캡슐) 하나를 바닥에 세웁니다. 새 씬의 카메라를 프리셋 자리에 다시 놓게 한다. */
        void populateScene( Scene& scene );

        const CameraPresetCatalog& getCatalog() const { return _catalog; }
        int32                      getSelectedIndex() const { return _selectedIndex; }
        /** @brief 고른 프리셋입니다. 고르지 않았으면 nullptr 입니다. */
        const CameraPresetDef* findSelectedPreset() const;
        /** @brief 프리셋 @p presetIndex 를 고르고 다음 `takeViewportPose` 가 카메라를 놓게 합니다. */
        void selectPreset( size_t presetIndex );
        /** @brief 슬라이더가 직접 고치는 대상 요(도)입니다. 고쳤으면 `markViewportDirty` 를 부릅니다. */
        float32& editTargetYawDegrees() { return _targetYawDegrees; }
        /** @brief 다음 `takeViewportPose` 가 카메라를 다시 놓게 합니다. */
        void          markViewportDirty() { _bViewportDirty = true; }
        const string& getStatus() const { return _status; }

        /**
         * @brief 카메라를 놓아야 하면(프리셋 · 대상 요 · 씬이 바뀌었다) 고른 프리셋을 미리보기 대상에 대해 풀어 뷰포트 자리를 냅니다. 낸 뒤에는 다시 바뀔 때까지 내지 않는다.
         * @param imageSize 뷰포트 이미지 크기(픽셀). 높이가 1 이하면 화면 비율은 대상 기본값이다.
         * @return 이번에 놓아야 하면 true 이고 @p outPose 를 씁니다.
         */
        [[nodiscard]] bool takeViewportPose( const float2& imageSize, CameraPresetViewportPose& outPose );

        /** @brief 프리셋 모드의 표시 이름입니다. */
        [[nodiscard]] static const utf8* getModeName( CameraPresetMode mode );

    private:
        CameraPresetCatalog _catalog;
        int32               _selectedIndex;
        float32             _targetYawDegrees; ///< Follow · FirstPerson · ThirdPerson 이 따르는 대상 요(도)
        bool                _bViewportDirty;   ///< 다음 `takeViewportPose` 가 뷰포트 카메라를 프리셋 자리에 놓는다
        string              _status;
    };
} // namespace sw::editor
