#include "pch.h"

#include "GameFramework/Plugin/Editor/CameraPresets/Model/CameraPresetModel.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include "GameFramework/Base/Character/Camera/CameraMode.h"

namespace sw::editor
{
    namespace
    {
        struct CameraPresetModelInternal
        {
            /// @brief 대상 캡슐의 높이(m)입니다 — 사람 크기로 서서 카메라가 무엇을 보는지 가늠하게 한다.
            static constexpr float32 kSubjectHeight = 1.8f;
            /// @brief 프리셋이 따르는 초점의 지면 위 높이(m)입니다(대상의 가운데 높이).
            static constexpr float32 kFocusHeight = 1.0f;
            /// @brief 미리보기 바닥의 높이입니다(`EditorPreviewEnvironment::_floorHeight` 기본값).
            static constexpr float32 kFloorHeight = -0.5f;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    CameraPresetModel::CameraPresetModel()
        : _catalog{}
        , _selectedIndex{ -1 }
        , _targetYawDegrees{ 0.0f }
        , _bViewportDirty{ false }
        , _status{}
    {
    }

    ToolAssetLoadResult CameraPresetModel::loadAsset( string_view assetPath )
    {
        _catalog.clear();
        _selectedIndex = -1;
        if ( _catalog.loadFromAsset( AssetID{ assetPath } ) == false )
        {
            _status = "The preset file could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        if ( _catalog.isEmpty() == false )
            _selectedIndex = 0;
        _bViewportDirty = true;
        _status         = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void CameraPresetModel::populateScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( "PreviewSubject" ) ) : nullptr;
        MeshComponent*     pMesh    = pSubject != nullptr ? pSubject->addComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
        {
            pMesh->setMeshID( "Capsule" );
            pMesh->setLocalPosition( float3{ 0.0f, CameraPresetModelInternal::kFloorHeight + CameraPresetModelInternal::kSubjectHeight * 0.5f, 0.0f } ); // 바닥에 선다
            pMesh->setLocalScale( float3{ 0.5f, CameraPresetModelInternal::kSubjectHeight * 0.5f, 0.5f } );
        }
        _bViewportDirty = true; // 새 씬의 카메라를 프리셋 자리에 다시 놓는다
    }

    const CameraPresetDef* CameraPresetModel::findSelectedPreset() const
    {
        if ( _selectedIndex < 0 || static_cast<size_t>( _selectedIndex ) >= _catalog.getCount() )
            return nullptr;
        return &_catalog.getAt( static_cast<size_t>( _selectedIndex ) );
    }

    void CameraPresetModel::selectPreset( size_t presetIndex )
    {
        if ( presetIndex >= _catalog.getCount() )
            return;
        _selectedIndex  = static_cast<int32>( presetIndex );
        _bViewportDirty = true;
    }

    bool CameraPresetModel::takeViewportPose( const float2& imageSize, CameraPresetViewportPose& outPose )
    {
        const CameraPresetDef* pPreset = findSelectedPreset();
        if ( _bViewportDirty == false || pPreset == nullptr )
            return false;
        _bViewportDirty = false;

        CameraTarget target{};
        target._focus = float3{ 0.0f, CameraPresetModelInternal::kFloorHeight + CameraPresetModelInternal::kFocusHeight, 0.0f };
        target._yaw   = _targetYawDegrees * MathUtil::kDegreeToRadian;
        if ( imageSize._y > 1.0f )
            target._aspectRatio = imageSize._x / imageSize._y;

        const CameraPose pose = evaluatePreset( *pPreset, target );
        // 포즈의 시선(+Z 를 회전으로 돌린 방향)을 뷰포트 요 · 피치 규약(시선 = (-sin 요 cos 피치, -sin 피치, -cos 요 cos 피치))으로 바꾼다.
        const float3 forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pose._rotation.toMatrix() ).normalize();
        outPose._position     = pose._position;
        outPose._pitch        = MathUtil::asin( MathUtil::clamp( -forward._y, -1.0f, 1.0f ) );
        outPose._yaw          = MathUtil::atan2( -forward._x, -forward._z );
        outPose._fieldOfViewY = pose._fieldOfViewY;
        return true;
    }

    const utf8* CameraPresetModel::getModeName( CameraPresetMode mode )
    {
        switch ( mode )
        {
            case CameraPresetMode::Fixed:
                return "Fixed";
            case CameraPresetMode::OrthoTopDown:
                return "OrthoTopDown";
            case CameraPresetMode::Orbit:
                return "Orbit";
            case CameraPresetMode::Follow:
                return "Follow";
            case CameraPresetMode::FirstPerson:
                return "FirstPerson";
            case CameraPresetMode::ThirdPerson:
                return "ThirdPerson";
            case CameraPresetMode::FollowXY:
                return "FollowXY";
            case CameraPresetMode::Dolly:
                return "Dolly";
        }
        return "?";
    }
} // namespace sw::editor
