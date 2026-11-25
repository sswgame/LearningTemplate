#include "pch.h"

#include "Editor/Panels/Scene/Model/PropertyShapeEdit.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Radians.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyEditConditionExpr.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyEditRequest.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/ObjectSnapshot.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/EnumInfo.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct PropertyShapeEditInternal
        {
            /** @brief 모양 선의 알파입니다 — 선택 윤곽 색을 조금 눌러 메시 윤곽과 가른다. */
            static constexpr float32 kLineAlpha = 0.85f;
            /** @brief 모양 선의 굵기(픽셀)입니다. */
            static constexpr float32 kLineThickness = 1.5f;

            /** @brief 메타 `Shape=` 의 글을 모양으로 바꿉니다. 모르는 글은 없음입니다. */
            static optional<PropertyShapeKind> findKind( const string& text )
            {
                if ( text == "Sphere" )
                    return PropertyShapeKind::Sphere;
                if ( text == "Box" )
                    return PropertyShapeKind::Box;
                if ( text == "Cone" )
                    return PropertyShapeKind::Cone;
                if ( text == "Arrow" )
                    return PropertyShapeKind::Arrow;
                if ( text == "Circle" )
                    return PropertyShapeKind::Circle;
                if ( text == "Capsule" )
                    return PropertyShapeKind::Capsule;
                if ( text == "Cylinder" )
                    return PropertyShapeKind::Cylinder;
                if ( text == "Points" )
                    return PropertyShapeKind::Points;
                return nullopt;
            }

            /** @brief 메타 `Shape=` 에서 여러 모양을 가르는 글자입니다(`Box|Cylinder`). */
            static constexpr string_view kKindSeparator = "|";

            /** @brief 메타 `ShapeAxes=` 의 글을 축 규칙으로 바꿉니다. 모르는 글은 없음입니다. */
            static optional<PropertyShapeAxes> findAxes( const string& text )
            {
                if ( text == "Local" )
                    return PropertyShapeAxes::Local;
                if ( text == "World" )
                    return PropertyShapeAxes::World;
                if ( text == "Yaw" )
                    return PropertyShapeAxes::Yaw;
                return nullopt;
            }

            /** @brief 열거 @p info 의 열거자 이름(표식 `Count` 제외) 집합이 모양 목록 @p listKind 와 같으면 true 입니다 — 고를 수 없는 모양 · 그릴 수 없는 열거자를 막는다. */
            static bool isSameKindSet( const EnumInfo& info, const vector<PropertyShapeKind>& listKind )
            {
                size_t matched = 0;
                for ( const EnumeratorInfo& enumerator : info._listEnumerator )
                {
                    const optional<PropertyShapeKind> kind = findKind( string( enumerator._name.c_str() ) );
                    if ( kind.has_value() == false || VectorUtil::contains( listKind, *kind ) == false )
                        return false;
                    ++matched;
                }
                return matched == listKind.size();
            }

            /**
             * @brief 축 규칙 @p axes 로 틀 @p inoutFrame 의 축을 바꿉니다(원점 · 배율은 그대로).
             * @details 요: 로컬 X 를 수평면에 눕힌 방향이 X, 월드 위가 Y, Z 는 X 를 요 회전으로 돌린 짝이다 — Detour 상자의 요(`NavMeshObstacleComponent::computeBoxYaw`)와 같은 축이다.
             */
            static void applyAxes( PropertyShapeAxes axes, PropertyShapeFrame& inoutFrame )
            {
                if ( axes == PropertyShapeAxes::Local )
                    return;
                if ( axes == PropertyShapeAxes::World )
                {
                    inoutFrame._axisX = float3::UnitX;
                    inoutFrame._axisY = float3::UnitY;
                    inoutFrame._axisZ = float3::UnitZ;
                    return;
                }
                const float3 flatX = float3{ inoutFrame._axisX._x, 0.0f, inoutFrame._axisX._z }.normalizeOr( float3::UnitX, MathUtil::kEpsilon );
                inoutFrame._axisX  = flatX;
                inoutFrame._axisY  = float3::UnitY;
                inoutFrame._axisZ  = float3{ -flatX._z, 0.0f, flatX._x };
            }

            /** @brief 메타 `ShapeScale=` 의 글을 규칙으로 바꿉니다. 모르는 글은 없음입니다. */
            static optional<PropertyShapeScaleRule> findScaleRule( const string& text )
            {
                if ( text == "None" )
                    return PropertyShapeScaleRule::None;
                if ( text == "Physics" )
                    return PropertyShapeScaleRule::Physics;
                if ( text == "Physics2D" )
                    return PropertyShapeScaleRule::Physics2D;
                if ( text == "Transform" )
                    return PropertyShapeScaleRule::Transform;
                return nullopt;
            }

            /**
             * @brief 규칙 @p rule 로 틀의 배율 @p scale 을 값 @p inoutValue 의 저장 → 월드 배율에 옮깁니다.
             * @details `PhysicsComponent::makeScaledShape`(3D · 2D)와 같은 식이다 — 캐릭터 컨트롤러가 캡슐을 그 함수로 짓는다. 그 함수의 규칙을 바꾸면 여기도 함께 바꾼다.
             */
            static void applyScaleRule( PropertyShapeScaleRule rule, const float3& scale, PropertyShapeValue& inoutValue )
            {
                if ( rule == PropertyShapeScaleRule::None )
                    return;
                if ( rule == PropertyShapeScaleRule::Transform )
                {
                    inoutValue._vectorScale = scale;
                    return;
                }
                const bool bPhysics2D = rule == PropertyShapeScaleRule::Physics2D;
                switch ( inoutValue._kind )
                {
                    case PropertyShapeKind::Box:
                        inoutValue._vectorScale = float3{ scale._x, scale._y, bPhysics2D ? 1.0f : scale._z };
                        break;
                    case PropertyShapeKind::Sphere:
                        inoutValue._numberScale = bPhysics2D ? MathUtil::max( scale._x, scale._y ) : MathUtil::max( MathUtil::max( scale._x, scale._z ), scale._y );
                        break;
                    case PropertyShapeKind::Circle:
                        inoutValue._numberScale = bPhysics2D ? MathUtil::max( scale._x, scale._y ) : MathUtil::max( scale._x, scale._z );
                        break;
                    case PropertyShapeKind::Capsule:
                        inoutValue._numberScale = bPhysics2D ? scale._x : MathUtil::max( scale._x, scale._z );
                        inoutValue._rangeScale  = scale._y;
                        break;
                    case PropertyShapeKind::Cylinder: // 반지름(x)은 캡슐처럼 max(X, Z), 반 높이(y)는 Y — 내비 장애물의 원기둥 식과 같다
                        inoutValue._vectorScale = float3{ bPhysics2D ? scale._x : MathUtil::max( scale._x, scale._z ), scale._y, 1.0f };
                        break;
                    case PropertyShapeKind::Cone:
                    case PropertyShapeKind::Arrow:
                    case PropertyShapeKind::Points:
                        break;
                }
            }

            /** @brief 각 형식(`Radians`)의 정규 이름입니다. 원뿔의 반각이 이 형식이다. */
            static const hashed_string& getAngleTypeName()
            {
                static const hashed_string s_radians( "Radians" );
                return s_radians;
            }

            /** @brief 모양 @p kind 가 받는 값 형식이면 true 입니다(상자 · 화살표 · 원기둥은 float3, 원뿔은 Radians, 점 목록은 vector<float3>, 나머지는 float32). */
            static bool isValueTypeAccepted( PropertyShapeKind kind, const PropertyInfo& prop )
            {
                static const hashed_string s_float32( "float32" );
                static const hashed_string s_float3( "float3" );
                static const hashed_string s_listFloat3( "vector<float3>" );
                if ( kind == PropertyShapeKind::Points )
                    return prop._typeName == s_listFloat3;
                if ( prop._bIsContainer == SW_TRUE || prop._bIsBitField == SW_TRUE )
                    return false;
                if ( kind == PropertyShapeKind::Cone )
                    return prop._typeName == getAngleTypeName();
                return prop._typeName == ( isVectorShapeKind( kind ) ? s_float3 : s_float32 );
            }

            /** @brief 컴포넌트의 모양 틀입니다 — 씬 컴포넌트면 그것, 아니면 오브젝트의 주 씬 컴포넌트의 월드 위치 · 회전(배율 없이). 없으면 없음입니다. */
            static optional<PropertyShapeFrame> findFrame( const Component& component )
            {
                const SceneComponent* pScene = isA<SceneComponent>( &component ) ? static_cast<const SceneComponent*>( &component ) : nullptr;
                if ( pScene == nullptr && component.getOwner() != nullptr )
                    pScene = component.getOwner()->getPrimarySceneComponent();
                if ( pScene == nullptr )
                    return nullopt;
                const float4x4     world = pScene->getWorldMatrix();
                const float3       axisX = float3::transformVector( float3::UnitX, world );
                const float3       axisY = float3::transformVector( float3::UnitY, world );
                const float3       axisZ = float3::transformVector( float3::UnitZ, world );
                PropertyShapeFrame frame{};
                frame._origin = pScene->getWorldPosition();
                frame._axisX  = axisX.normalizeOr( float3::UnitX, MathUtil::kEpsilon );
                frame._axisY  = axisY.normalizeOr( float3::UnitY, MathUtil::kEpsilon );
                frame._axisZ  = axisZ.normalizeOr( float3::UnitZ, MathUtil::kEpsilon );
                frame._scale  = float3{ axisX.getLength(), axisY.getLength(), axisZ.getLength() }; // 행렬 행의 길이 — `float4x4::getScale` 과 같은 크기
                return frame;
            }

            /** @brief 오브젝트 @p object 의 선언형 모양마다 @p visit( 컴포넌트, 선언, 값 ) 을 부릅니다. */
            template <typename Visit>
            static void forEachShape( const GameObject& object, PropertyShapeHandleSession& session, vector<PropertyShapeDecl>& listDecl, Visit&& visit )
            {
                object.forEachComponent( [&]( Component* pComponent )
                {
                    const TypeInfo* pType = pComponent->getTypeInfo();
                    if ( pType == nullptr || pComponent->isActive() == false )
                        return;
                    session.collectDeclarations( *pType, listDecl );
                    if ( listDecl.empty() )
                        return;
                    const optional<PropertyShapeFrame> frame = findFrame( *pComponent );
                    if ( frame.has_value() == false )
                        return;
                    for ( const PropertyShapeDecl& decl : listDecl )
                    {
                        // 조건이 거짓인 프로퍼티(모양 열거가 다른 쪽을 고른 부피)는 인스펙터가 막는 것처럼 그리지도 끌지도 않는다.
                        if ( PropertyEditConditionExpr::getEditState( *decl._pProperty, ObjectAddress{ pComponent } ) != PropertyEditState::Enabled )
                            continue;
                        if ( decl.findSelectedKind( pComponent ).has_value() == false )
                            continue; // 고르는 열거가 열거자 밖 값이다

                        visit( *pComponent, decl, decl.makeValue( pComponent, *frame ) );
                    }
                } );
            }

            /** @brief 감시 값 `Editor.PropertyShapeNumber` — 메인 선택(하나)의 첫 float32 모양 값(반지름 · 반각)입니다. 없으면 0 입니다. */
            [[nodiscard]] static bool readFirstNumber( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue                  = 0.0;
                const GameObject* pObject = editor::findSubsystem<EditorContext>() != nullptr ? editor::getMainSceneSelection().getPrimaryObject() : nullptr;
                if ( pObject == nullptr )
                    return true;
                vector<PropertyShapeDecl> listDecl;
                bool                      bFound = false;
                forEachShape( *pObject, PropertyShapeHandleSession::get(), listDecl, [&]( const Component& /*component*/, const PropertyShapeDecl& /*decl*/, const PropertyShapeValue& value )
                {
                    const bool bNumber = isVectorShapeKind( value._kind ) == false && value._kind != PropertyShapeKind::Points;
                    if ( bFound || bNumber == false )
                        return;
                    outValue = static_cast<float64>( value._number );
                    bFound   = true;
                } );
                return true;
            }

            /** @brief 감시 값 `Editor.PropertyShapePointSum` — 메인 선택(하나)의 첫 점 목록의 저장 값 성분 합입니다. 없으면 0 입니다. */
            [[nodiscard]] static bool readFirstPointSum( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue                  = 0.0;
                const GameObject* pObject = editor::findSubsystem<EditorContext>() != nullptr ? editor::getMainSceneSelection().getPrimaryObject() : nullptr;
                if ( pObject == nullptr )
                    return true;
                vector<PropertyShapeDecl> listDecl;
                bool                      bFound = false;
                forEachShape( *pObject, PropertyShapeHandleSession::get(), listDecl, [&]( const Component& /*component*/, const PropertyShapeDecl& /*decl*/, const PropertyShapeValue& value )
                {
                    if ( bFound || value._kind != PropertyShapeKind::Points )
                        return;
                    for ( const float3& point : value._listPoint )
                    {
                        outValue += static_cast<float64>( point._x + point._y + point._z );
                    }
                    bFound = true;
                } );
                return true;
            }

            [[nodiscard]] static bool readCommitCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( PropertyShapeHandleSession::get()._edit.getCommitCount() );
                return true;
            }

            [[nodiscard]] static bool readDrawnHandleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( PropertyShapeHandleSession::get()._drawnHandleCount );
                return true;
            }
        };
    } // namespace

    expected<void> PropertyShapeDecl::collect( const TypeInfo& type, vector<PropertyShapeDecl>& outListDecl )
    {
        using Internal = PropertyShapeEditInternal;
        static const hashed_string s_shapeKey( "Shape" );
        static const hashed_string s_rangeKey( "ShapeRange" );
        static const hashed_string s_axesKey( "ShapeAxes" );
        static const hashed_string s_originKey( "ShapeOrigin" );
        static const hashed_string s_scaleKey( "ShapeScale" );
        static const hashed_string s_centerKey( "ShapeCenter" );
        static const hashed_string s_selectorKey( "ShapeSelector" );
        static const hashed_string s_float3( "float3" );
        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            const string* pShape = prop.findCustomMeta( s_shapeKey );
            if ( pShape == nullptr )
                continue;
            // `Box|Cylinder` — 여럿이면 고르는 열거가 정한다.
            vector<PropertyShapeKind> listKind;
            for ( const string_view part : StringUtil::split( *pShape, Internal::kKindSeparator ) )
            {
                const optional<PropertyShapeKind> kind = Internal::findKind( string( StringUtil::trim( part ) ) );
                if ( kind.has_value() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "unknown Shape (Sphere, Box, Cone, Arrow, Circle, Capsule, Cylinder, Points)", prop._name.view() } );
                if ( Internal::isValueTypeAccepted( *kind, prop ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "Shape value type mismatch (Box/Arrow/Cylinder: float3, Cone: Radians, Points: vector<float3>, the others: float32)", prop._name.view() } );
                listKind.push_back( *kind );
            }
            PropertyShapeDecl decl{};
            decl._pProperty = &prop;
            decl._kind      = listKind.front();
            if ( const string* pSelector = prop.findCustomMeta( s_selectorKey ); pSelector != nullptr )
            {
                const PropertyInfo* pSelectorProp = type.findPropertyInHierarchy( hashed_string{ pSelector->c_str() } );
                const EnumInfo*     pEnum         = pSelectorProp != nullptr ? editor::getSubsystem<TypeRegistry>().findEnum( pSelectorProp->_typeName ) : nullptr;
                if ( pEnum == nullptr || Internal::isSameKindSet( *pEnum, listKind ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeSelector needs an enum property whose enumerator names are exactly the listed shapes", prop._name.view() } );
                decl._pSelectorProperty = pSelectorProp;
                decl._pSelectorEnum     = pEnum;
            }
            else if ( listKind.size() > 1 )
            {
                return unexpected( Error{ ErrorKind::InvalidArgument, "Shape=A|B needs ShapeSelector=<enum property>", prop._name.view() } );
            }
            const bool    bSelected = decl._pSelectorProperty != nullptr;
            const bool    bRange    = decl._kind == PropertyShapeKind::Cone || decl._kind == PropertyShapeKind::Capsule;
            const string* pRange    = prop.findCustomMeta( s_rangeKey );
            if ( bRange && bSelected == false )
            {
                const PropertyInfo* pRangeProp = pRange != nullptr ? type.findPropertyInHierarchy( hashed_string{ pRange->c_str() } ) : nullptr;
                if ( pRangeProp == nullptr || Internal::isValueTypeAccepted( PropertyShapeKind::Sphere, *pRangeProp ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "Shape=Cone/Capsule needs ShapeRange=<float32 property of the same type>", prop._name.view() } );
                decl._pRangeProperty = pRangeProp;
            }
            else if ( pRange != nullptr || ( bRange && bSelected ) )
            {
                return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeRange is only for a single Shape=Cone/Capsule", prop._name.view() } );
            }
            if ( const string* pAxes = prop.findCustomMeta( s_axesKey ); pAxes != nullptr )
            {
                const optional<PropertyShapeAxes> axes = Internal::findAxes( *pAxes );
                if ( axes.has_value() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeAxes is Local, World or Yaw", prop._name.view() } );
                decl._axes = *axes;
            }
            if ( const string* pOrigin = prop.findCustomMeta( s_originKey ); pOrigin != nullptr )
            {
                if ( decl._kind != PropertyShapeKind::Capsule || bSelected || ( *pOrigin != "Base" && *pOrigin != "Center" ) )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeOrigin is Base or Center, only for a single Shape=Capsule", prop._name.view() } );
                decl._bBaseOrigin = *pOrigin == "Base" ? 1 : 0;
            }
            if ( const string* pCenter = prop.findCustomMeta( s_centerKey ); pCenter != nullptr )
            {
                const PropertyInfo* pCenterProp = type.findPropertyInHierarchy( hashed_string{ pCenter->c_str() } );
                if ( pCenterProp == nullptr || pCenterProp->_typeName != s_float3 || pCenterProp->_bIsContainer == SW_TRUE )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeCenter needs a float3 property of the same type", prop._name.view() } );
                decl._pCenterProperty = pCenterProp;
            }
            if ( const string* pScale = prop.findCustomMeta( s_scaleKey ); pScale != nullptr )
            {
                const optional<PropertyShapeScaleRule> rule      = Internal::findScaleRule( *pScale );
                const bool                             bScalable = VectorUtil::containsIf( listKind, []( PropertyShapeKind kind ) // 반각 · 방향은 배율과 상관없다
                                            { return kind == PropertyShapeKind::Cone || kind == PropertyShapeKind::Arrow; } ) == false;
                const bool                             bPoints   = VectorUtil::contains( listKind, PropertyShapeKind::Points );
                const bool                             bFits     = rule.has_value() && ( *rule == PropertyShapeScaleRule::None || ( *rule == PropertyShapeScaleRule::Transform ) == bPoints );
                if ( bFits == false || bScalable == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeScale is None, Physics or Physics2D, not for Shape=Cone/Arrow; Shape=Points takes None or Transform", prop._name.view() } );
                decl._scaleRule = *rule;
            }
            outListDecl.push_back( decl );
        }
        return {};
    }

    optional<PropertyShapeKind> PropertyShapeDecl::findSelectedKind( const void* pInstance ) const
    {
        if ( _pSelectorProperty == nullptr )
            return _kind;
        const void* pValue = _pSelectorProperty->getRawPtr( pInstance );
        if ( pValue == nullptr )
            return nullopt;
        const hashed_string name = _pSelectorEnum->getNameByValue( _pSelectorEnum->readValueFromMemory( pValue ) );
        return PropertyShapeEditInternal::findKind( string( name.c_str() ) );
    }

    PropertyShapeValue PropertyShapeDecl::makeValue( const void* pInstance, const PropertyShapeFrame& frame ) const
    {
        PropertyShapeValue value{};
        value._frame       = frame;
        value._kind        = findSelectedKind( pInstance ).value_or( _kind ); // 열거자 밖 값은 부르는 쪽(`forEachShape`)이 미리 거른다
        value._bBaseOrigin = _bBaseOrigin;
        if ( _pCenterProperty != nullptr )
        {
            // 쓰는 쪽은 `float3::transform( _center, world )` 다 — 축(회전) × 배율 크기가 행렬의 행이라 같은 점이다.
            const float3* pCenter = _pCenterProperty->getValuePtr<float3>( pInstance );
            if ( pCenter != nullptr )
                value._frame._origin = frame._origin + frame.toWorldVector( float3::multiply( *pCenter, frame._scale ) );
        }
        PropertyShapeEditInternal::applyAxes( _axes, value._frame );
        if ( isVectorShapeKind( value._kind ) )
        {
            const float3* pVector = _pProperty->getValuePtr<float3>( pInstance );
            value._vector         = pVector != nullptr ? *pVector : float3{};
        }
        else if ( value._kind == PropertyShapeKind::Cone )
        {
            const Radians* pAngle = _pProperty->getValuePtr<Radians>( pInstance );
            value._number         = pAngle != nullptr ? pAngle->radians() : 0.0f;
        }
        else if ( value._kind == PropertyShapeKind::Points )
        {
            const vector<float3>* pListPoint = _pProperty->getValuePtr<vector<float3>>( pInstance );
            if ( pListPoint != nullptr )
                value._listPoint = *pListPoint;
        }
        else
        {
            const float32* pNumber = _pProperty->getValuePtr<float32>( pInstance );
            value._number          = pNumber != nullptr ? *pNumber : 0.0f;
        }
        if ( _pRangeProperty != nullptr )
        {
            const float32* pRange = _pRangeProperty->getValuePtr<float32>( pInstance );
            value._range          = pRange != nullptr ? *pRange : 0.0f;
        }
        PropertyShapeEditInternal::applyScaleRule( _scaleRule, frame._scale, value );
        return value;
    }

    PropertyShapeHandleEdit::PropertyShapeHandleEdit()
        : _pBefore{}
        , _undoLabel{}
        , _object{}
        , _commitCount{ 0 }
        , _bChanged{ 0 }
    {
    }

    PropertyShapeHandleEdit::~PropertyShapeHandleEdit() = default;

    void PropertyShapeHandleEdit::gather( GameObjectHandle dragged, PropertyShapeHandleViewState& outState )
    {
        outState._object     = GameObjectHandle{};
        outState._bAvailable = false;
        outState._listHandle.clear();
        GameObject* pObject = nullptr;
        if ( dragged.isValid() )
            pObject = editor::findGameObject( dragged );
        else if ( editor::getMainSceneSelection().getSelectedObjectCount() == 1 )
            pObject = editor::getMainSceneSelection().getPrimaryObject();
        if ( pObject == nullptr || pObject->isPendingDestroy() || EditorPlaySession::areSceneEditsAllowed() == false )
            return;
        PropertyShapeHandleSession& session = PropertyShapeHandleSession::get();
        vector<PropertyShapeDecl>   listDecl;
        PropertyShapeEditInternal::forEachShape( *pObject, session, listDecl, [&]( const Component& component, const PropertyShapeDecl& decl, const PropertyShapeValue& value )
        {
            const hashed_string rangeName = decl._pRangeProperty != nullptr ? decl._pRangeProperty->_name : hashed_string{};
            value.collectHandles( component.getComponentID(), decl._pProperty->_name, rangeName, outState._listHandle );
        } );
        outState._object     = pObject->getHandle();
        outState._bAvailable = outState._listHandle.empty() == false;
    }

    bool PropertyShapeHandleEdit::applyDrag( Component& component, const PropertyShapeHandle& handle, const Ray& ray, const TypeRegistry& registry )
    {
        const TypeInfo*     pType = component.getTypeInfo();
        const PropertyInfo* pProp = pType != nullptr ? pType->findPropertyInHierarchy( handle._property ) : nullptr;
        if ( pProp == nullptr )
            return false;
        PropertyEditRequest request{};
        request._path._root = handle._property;
        bool bChanged       = false;
        if ( handle.isVectorValue() )
        {
            const float3* pCurrent = pProp->getValuePtr<float3>( &component );
            const float3  dragged  = handle.computeDraggedVector( ray );
            if ( pCurrent == nullptr || *pCurrent == dragged )
                return false;
            request._kind       = PropertyEditKind::SetValue;
            request._value      = TaskValue{ dragged };
            const float3 before = *pCurrent;
            bChanged            = request.applyTo( MutableObjectAddress{ &component }, *pType, registry ) && *pProp->getValuePtr<float3>( &component ) != before;
        }
        else if ( handle._kind == PropertyShapeHandleKind::Point )
        {
            const vector<float3>* pListPoint = pProp->getValuePtr<vector<float3>>( &component );
            const float3          dragged    = handle.computeDraggedVector( ray );
            if ( pListPoint == nullptr || handle._elementIndex >= pListPoint->size() || ( *pListPoint )[handle._elementIndex] == dragged )
                return false; // 끄는 사이 점이 사라졌다
            request._kind  = PropertyEditKind::SetValue;
            request._value = TaskValue{ dragged };
            request._path._listNode.push_back( PropertyPathNode{ hashed_string{}, handle._elementIndex, PropertyPathStep::Element } );
            bChanged = request.applyTo( MutableObjectAddress{ &component }, *pType, registry );
        }
        else if ( pProp->_typeName == PropertyShapeEditInternal::getAngleTypeName() )
        {
            const Radians* pCurrent = pProp->getValuePtr<Radians>( &component );
            if ( pCurrent == nullptr )
                return false;
            const Radians before = *pCurrent;
            request._kind        = PropertyEditKind::SetNumber; // 각의 숫자는 도다(`PackedValueTraits<Radians>`) — 끈 라디안을 도로 넘겨 범위도 도로 자른다
            request._number      = static_cast<float64>( handle.computeDraggedNumber( ray ) ) * Radians::kDegreesPerRadian;
            bChanged             = request.applyTo( MutableObjectAddress{ &component }, *pType, registry ) && *pProp->getValuePtr<Radians>( &component ) != before;
        }
        else
        {
            const float32* pCurrent = pProp->getValuePtr<float32>( &component );
            if ( pCurrent == nullptr )
                return false;
            const float32 before = *pCurrent;
            request._kind        = PropertyEditKind::SetNumber; // 허용 범위(`Min` · `Max`)로 자른다 — 인스펙터 칸과 같은 길
            request._number      = static_cast<float64>( handle.computeDraggedNumber( ray ) );
            bChanged             = request.applyTo( MutableObjectAddress{ &component }, *pType, registry ) && *pProp->getValuePtr<float32>( &component ) != before;
        }
        if ( bChanged == false )
            return false;
        // 인스펙터의 쓰기 길(`PropertyGridViewModel` 의 씬 편집 적용)과 같다 — 알림을 빠뜨리면 값에 반응하는 상태(렌더 · 물리)가 어긋난다.
        component.onPropertyChanged( handle._property );
        if ( component.getOwner() != nullptr )
            (void)ObjectValidation::reportGameObject( *component.getOwner(), false );
        return true;
    }

    void PropertyShapeHandleEdit::apply( const vector<PropertyShapeHandleCommand>& listCommand )
    {
        for ( const PropertyShapeHandleCommand& command : listCommand )
        {
            switch ( command._kind )
            {
                case PropertyShapeHandleCommandKind::Capture:
                    capture( command );
                    break;
                case PropertyShapeHandleCommandKind::Drag:
                    drag( command );
                    break;
                case PropertyShapeHandleCommandKind::End:
                    end( command._bCommit );
                    break;
            }
        }
    }

    void PropertyShapeHandleEdit::capture( const PropertyShapeHandleCommand& command )
    {
        GameObject*         pObject    = editor::findGameObject( command._object );
        const Component*    pComponent = pObject != nullptr ? pObject->findComponentByID( command._handle._componentID ) : nullptr;
        const TypeInfo*     pType      = pComponent != nullptr ? pComponent->getTypeInfo() : nullptr;
        const PropertyInfo* pProp      = pType != nullptr ? pType->findPropertyInHierarchy( command._handle._property ) : nullptr;
        const string        display    = pProp != nullptr ? pProp->_metadata->getEditor()._displayName : string{};
        _object                        = command._object;
        _pBefore                       = pObject != nullptr ? make_unique<ObjectSnapshot>( EditorTransaction::captureSnapshot( pObject ) ) : unique_ptr<ObjectSnapshot>{};
        _undoLabel                     = "Edit ";
        _undoLabel += display.empty() ? string{ command._handle._property.view() } : display;
        _bChanged = 0;
    }

    void PropertyShapeHandleEdit::drag( const PropertyShapeHandleCommand& command )
    {
        GameObject* pObject    = editor::findGameObject( command._object );
        Component*  pComponent = pObject != nullptr ? pObject->findComponentByID( command._handle._componentID ) : nullptr;
        if ( pComponent != nullptr && applyDrag( *pComponent, command._handle, command._ray, editor::getSubsystem<TypeRegistry>() ) )
            _bChanged = 1;
    }

    void PropertyShapeHandleEdit::end( bool bCommit )
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        const bool        bCommitDrag = bCommit && _bChanged != 0 && _pBefore != nullptr && _pBefore->_xml.empty() == false && EditorPlaySession::areSceneEditsAllowed();
        if ( bCommitDrag )
        {
            GameObject* pObject = editor::findGameObject( _object );
            if ( pObject != nullptr )
            {
                EditorSceneCommands::commitModify( transaction, pObject, *_pBefore, _undoLabel );
                ++_commitCount;
            }
        }
        _object = GameObjectHandle{};
        _pBefore.reset();
        _bChanged = 0;
    }

    PropertyShapeHandleSession& PropertyShapeHandleSession::get()
    {
        return getEditorState<PropertyShapeHandleSession>();
    }

    void PropertyShapeHandleSession::collectDeclarations( const TypeInfo& type, vector<PropertyShapeDecl>& outListDecl )
    {
        outListDecl.clear();
        const expected<void> result = PropertyShapeDecl::collect( type, outListDecl );
        if ( result.has_value() )
            return;
        outListDecl.clear();
        if ( VectorUtil::contains( _listReportedType, &type ) )
            return;
        _listReportedType.push_back( &type );
        SW_LOG_ERROR_ID( "PropertyShape.InvalidDeclaration", "%#: %# (%#) - no shape is drawn for this type", type._fullyQualifiedName.c_str(), result.error().message(),
                         result.error().detail() );
    }

    void PropertyShapeGatherer::gather( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList )
    {
        if ( args._pObjectManager == nullptr )
            return;
        EditorVisualizerLine prototype{};
        prototype._thickness                = PropertyShapeEditInternal::kLineThickness;
        prototype._colorRole                = EditorVisualizerColorRole::SelectionOutline; // 선택한 오브젝트의 값이다 — 강조색은 선택에만(에디터 화면 규칙)
        prototype._color._w                 = PropertyShapeEditInternal::kLineAlpha;
        PropertyShapeHandleSession& session = PropertyShapeHandleSession::get();
        vector<PropertyShapeDecl>   listDecl;
        for ( const GameObjectHandle handle : editor::getMainSceneSelection().getSelectedHandles() )
        {
            const GameObject* pObject = editor::findGameObject( handle );
            if ( pObject == nullptr || pObject->isPendingDestroy() || pObject->isActiveInHierarchy() == false )
                continue;
            PropertyShapeEditInternal::forEachShape( *pObject, session, listDecl, [&]( const Component& /*component*/, const PropertyShapeDecl& /*decl*/, const PropertyShapeValue& value )
            { value.addLines( prototype, outDrawList ); } );
        }
    }

    SW_AUTOMATION_WATCH( editorPropertyShapeCommitCount, "Editor.PropertyShapeCommitCount", "Declared property shape handle drags that finished and left an undo entry",
                         &PropertyShapeEditInternal::readCommitCount );
    SW_AUTOMATION_WATCH( editorPropertyShapeHandleCount, "Editor.PropertyShapeHandleCount", "Declared property shape handles drawn in the last frame",
                         &PropertyShapeEditInternal::readDrawnHandleCount );
    SW_AUTOMATION_WATCH( editorPropertyShapeNumber, "Editor.PropertyShapeNumber", "First float32 declared shape value (radius, half angle) of the single selected object (0 when none)",
                         &PropertyShapeEditInternal::readFirstNumber );
    SW_AUTOMATION_WATCH( editorPropertyShapePointSum, "Editor.PropertyShapePointSum", "Sum of the stored components of the first declared point list of the single selected object (0 when none)",
                         &PropertyShapeEditInternal::readFirstPointSum );
} // namespace sw::editor
