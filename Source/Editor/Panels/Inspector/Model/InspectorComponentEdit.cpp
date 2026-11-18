#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorComponentEdit.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Visual/2D/SpriteComponent.h"

namespace sw::editor
{
    namespace
    {
        struct InspectorComponentEditInternal
        {
            /** @brief 거래가 필요 없는 컴포넌트 쓰기 @p edit(`void( Component& )`)를 요청으로 넘깁니다 — 되돌리기는 인스펙터 틀의 확장 영역 구간이 남긴다. */
            template <typename TEdit>
            static void requestComponentWrite( const EditorPropertyGridTarget& target, const TEdit& edit )
            {
                InspectorComponentEdit::requestComponentEdit( target, InspectorComponentEdit::ComponentEditDelegate::create( [edit]( Component& component, EditorTransaction& /*transaction*/ )
                { edit( component ); } ) );
            }
        };
    } // namespace

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

    const Component* InspectorComponentEdit::findComponent( const EditorPropertyGridTarget& target )
    {
        const GameObject* pObject = PropertyGridModel::findSceneObject( target._scope, target._objectID );
        return pObject != nullptr ? pObject->findComponentByID( target._componentID ) : nullptr;
    }

    void InspectorComponentEdit::requestComponentEdit( const EditorPropertyGridTarget& target, const ComponentEditDelegate& edit )
    {
        if ( edit.isBound() == false )
            return;
        const EditorPropertyGridScope scope       = target._scope;
        const uint64                  objectID    = target._objectID;
        const uint64                  componentID = target._componentID;
        EditorCommandRegistry::getInEditorContext().requestSceneEdit( Delegate<void( EditorTransaction& )>::create( [scope, objectID, componentID, edit]( EditorTransaction& transaction )
        {
            GameObject* pObject    = PropertyGridModel::findSceneObject( scope, objectID );
            Component*  pComponent = ( pObject != nullptr && pObject->isPendingDestroy() == false ) ? pObject->findComponentByID( componentID ) : nullptr;
            if ( pComponent != nullptr ) // 그 사이 사라졌으면 아무것도 하지 않는다
                edit( *pComponent, transaction );
        } ) );
    }

    bool InspectorComponentEdit::gatherCamera( const EditorPropertyGridTarget& target, CameraSectionValues& outValues )
    {
        const Component* pComponent = findComponent( target );
        if ( pComponent != nullptr )
            gatherCamera( *pComponent, outValues );
        return pComponent != nullptr;
    }

    void InspectorComponentEdit::applyCamera( const EditorPropertyGridTarget& target, const CameraSectionValues& values )
    {
        InspectorComponentEditInternal::requestComponentWrite( target, [values]( Component& component )
        { applyCamera( component, values ); } );
    }

    bool InspectorComponentEdit::gatherTransform( const EditorPropertyGridTarget& target, TransformSectionValues& outValues )
    {
        const Component* pComponent = findComponent( target );
        if ( pComponent != nullptr )
            gatherTransform( *pComponent, outValues );
        return pComponent != nullptr;
    }

    void InspectorComponentEdit::setLocalPosition( const EditorPropertyGridTarget& target, const float3& position )
    {
        InspectorComponentEditInternal::requestComponentWrite( target, [position]( Component& component )
        { setLocalPosition( component, position ); } );
    }

    void InspectorComponentEdit::setLocalRotationDegrees( const EditorPropertyGridTarget& target, const float3& rotationDegrees )
    {
        InspectorComponentEditInternal::requestComponentWrite( target, [rotationDegrees]( Component& component )
        { setLocalRotationDegrees( component, rotationDegrees ); } );
    }

    void InspectorComponentEdit::setLocalScale( const EditorPropertyGridTarget& target, const float3& scale )
    {
        InspectorComponentEditInternal::requestComponentWrite( target, [scale]( Component& component )
        { setLocalScale( component, scale ); } );
    }

    void InspectorComponentEdit::collectTags( const EditorPropertyGridTarget& target, vector<string>& outListTag )
    {
        outListTag.clear();
        if ( const Component* pComponent = findComponent( target ) )
            collectTags( *pComponent, outListTag );
    }

    void InspectorComponentEdit::openSpriteClipTool()
    {
        editor::getSubsystem<EditorWorkspace>().requestOpenDocumentPanel( EditorAssetType{ SpriteClipAsset::kAssetType } );
    }
} // namespace sw::editor
