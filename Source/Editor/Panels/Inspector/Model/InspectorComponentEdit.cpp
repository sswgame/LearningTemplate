#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorComponentEdit.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Visual/2D/SpriteComponent.h"

namespace sw::editor
{
    const TypeInfo* InspectorComponentEdit::getCameraComponentType()
    {
        return CameraComponent::StaticType();
    }

    const TypeInfo* InspectorComponentEdit::getSceneComponentType()
    {
        return SceneComponent::StaticType();
    }

    const TypeInfo* InspectorComponentEdit::getTagComponentType()
    {
        return TagComponent::StaticType();
    }

    const TypeInfo* InspectorComponentEdit::getSpriteComponentType()
    {
        return SpriteComponent::StaticType();
    }

    void InspectorComponentEdit::gatherCamera( const Component& component, CameraSectionValues& outValues )
    {
        const CameraComponent& camera = static_cast<const CameraComponent&>( component ); // 인스펙터 틀이 등록한 형(과 하위 형)에만 부른다
        outValues._fovYDegrees        = MathUtil::toDegree( camera.getFieldOfViewY() );
        outValues._nearPlane          = camera.getNearPlane();
        outValues._farPlane           = camera.getFarPlane();
        outValues._orthoHeight        = camera.getOrthoHeight();
        outValues._bOrthographic      = camera.isOrthographic() ? SW_TRUE : SW_FALSE;
    }

    void InspectorComponentEdit::applyCamera( Component& component, const CameraSectionValues& values )
    {
        // 바뀐 칸만 넣는다 — 도 ↔ 라디안 왕복이 고치지 않은 시야각을 조금씩 바꾸지 않게 한다.
        CameraSectionValues current;
        gatherCamera( component, current );
        CameraComponent& camera = static_cast<CameraComponent&>( component );
        if ( values._fovYDegrees != current._fovYDegrees )
            camera.setFieldOfViewY( MathUtil::toRadian( values._fovYDegrees ) );
        if ( values._nearPlane != current._nearPlane )
            camera.setNearPlane( values._nearPlane );
        if ( values._farPlane != current._farPlane )
            camera.setFarPlane( values._farPlane );
        if ( values._bOrthographic != current._bOrthographic )
            camera.setOrthographic( values._bOrthographic != SW_FALSE );
        if ( values._bOrthographic != SW_FALSE && values._orthoHeight != current._orthoHeight )
            camera.setOrthoHeight( values._orthoHeight );
    }

    void InspectorComponentEdit::gatherTransform( const Component& component, TransformSectionValues& outValues )
    {
        const SceneComponent& scene     = static_cast<const SceneComponent&>( component );
        outValues._localPosition        = scene.getLocalPosition();
        outValues._localRotationDegrees = scene.getLocalRotation() * MathUtil::kRadianToDegree;
        outValues._localScale           = scene.getLocalScale();
        outValues._worldPosition        = scene.getWorldPosition();
    }

    void InspectorComponentEdit::setLocalPosition( Component& component, const float3& position )
    {
        static_cast<SceneComponent&>( component ).setLocalPosition( position );
    }

    void InspectorComponentEdit::setLocalRotationDegrees( Component& component, const float3& rotationDegrees )
    {
        static_cast<SceneComponent&>( component ).setLocalRotation( rotationDegrees * MathUtil::kDegreeToRadian );
    }

    void InspectorComponentEdit::setLocalScale( Component& component, const float3& scale )
    {
        static_cast<SceneComponent&>( component ).setLocalScale( scale );
    }

    void InspectorComponentEdit::collectTags( const Component& component, vector<string>& outListTag )
    {
        outListTag.clear();
        for ( const TagID& tag : static_cast<const TagComponent&>( component ).getTags().getTags() )
        {
            if ( StringUtil::isNullOrEmpty( tag._pString ) == false )
                outListTag.emplace_back( tag._pString );
        }
    }

    void InspectorComponentEdit::openSpriteClipTool()
    {
        editor::getSubsystem<EditorWorkspace>().requestOpenDocumentPanel( EditorAssetType{ SpriteClipAsset::kAssetType } );
    }
} // namespace sw::editor
