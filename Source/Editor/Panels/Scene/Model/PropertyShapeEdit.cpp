#include "pch.h"

#include "Editor/Panels/Scene/Model/PropertyShapeEdit.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/ObjectSnapshot.h"
#include "Editor/Panels/Inspector/Model/PropertyEditConditionExpr.h"
#include "Editor/Panels/Inspector/Model/PropertyEditRequest.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
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
                return nullopt;
            }

            /** @brief 모양 @p kind 가 받는 값 형식이면 true 입니다(상자 · 화살표는 float3, 나머지는 float32). */
            static bool isValueTypeAccepted( PropertyShapeKind kind, const PropertyInfo& prop )
            {
                static const hashed_string s_float32( "float32" );
                static const hashed_string s_float3( "float3" );
                if ( prop._bIsContainer == SW_TRUE || prop._bIsBitField == SW_TRUE )
                    return false;
                const bool bVector = kind == PropertyShapeKind::Box || kind == PropertyShapeKind::Arrow;
                return prop._typeName == ( bVector ? s_float3 : s_float32 );
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
                PropertyShapeFrame frame{};
                frame._origin = pScene->getWorldPosition();
                frame._axisX  = float3::transformVector( float3::UnitX, world ).normalizeOr( float3::UnitX, MathUtil::kEpsilon );
                frame._axisY  = float3::transformVector( float3::UnitY, world ).normalizeOr( float3::UnitY, MathUtil::kEpsilon );
                frame._axisZ  = float3::transformVector( float3::UnitZ, world ).normalizeOr( float3::UnitZ, MathUtil::kEpsilon );
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
                    const bool bNumber = value._kind != PropertyShapeKind::Box && value._kind != PropertyShapeKind::Arrow;
                    if ( bFound || bNumber == false )
                        return;
                    outValue = static_cast<float64>( value._number );
                    bFound   = true;
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
        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            const string* pShape = prop.findCustomMeta( s_shapeKey );
            if ( pShape == nullptr )
                continue;
            const optional<PropertyShapeKind> kind = Internal::findKind( *pShape );
            if ( kind.has_value() == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "unknown Shape (Sphere, Box, Cone, Arrow, Circle, Capsule)", prop._name.view() } );
            if ( Internal::isValueTypeAccepted( *kind, prop ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "Shape value type mismatch (Box/Arrow: float3, the others: float32)", prop._name.view() } );
            PropertyShapeDecl decl{};
            decl._pProperty      = &prop;
            decl._kind           = *kind;
            const bool    bRange = *kind == PropertyShapeKind::Cone || *kind == PropertyShapeKind::Capsule;
            const string* pRange = prop.findCustomMeta( s_rangeKey );
            if ( bRange )
            {
                const PropertyInfo* pRangeProp = pRange != nullptr ? type.findPropertyInHierarchy( hashed_string{ pRange->c_str() } ) : nullptr;
                if ( pRangeProp == nullptr || Internal::isValueTypeAccepted( PropertyShapeKind::Sphere, *pRangeProp ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "Shape=Cone/Capsule needs ShapeRange=<float32 property of the same type>", prop._name.view() } );
                decl._pRangeProperty = pRangeProp;
            }
            else if ( pRange != nullptr )
            {
                return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeRange is only for Shape=Cone/Capsule", prop._name.view() } );
            }
            if ( const string* pAxes = prop.findCustomMeta( s_axesKey ); pAxes != nullptr )
            {
                if ( *pAxes != "World" && *pAxes != "Local" )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeAxes is World or Local", prop._name.view() } );
                decl._bWorldAxes = *pAxes == "World" ? 1 : 0;
            }
            if ( const string* pOrigin = prop.findCustomMeta( s_originKey ); pOrigin != nullptr )
            {
                if ( *kind != PropertyShapeKind::Capsule || ( *pOrigin != "Base" && *pOrigin != "Center" ) )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "ShapeOrigin is Base or Center, only for Shape=Capsule", prop._name.view() } );
                decl._bBaseOrigin = *pOrigin == "Base" ? 1 : 0;
            }
            outListDecl.push_back( decl );
        }
        return {};
    }

    PropertyShapeValue PropertyShapeDecl::makeValue( const void* pInstance, const PropertyShapeFrame& frame ) const
    {
        PropertyShapeValue value{};
        value._frame       = frame;
        value._kind        = _kind;
        value._bBaseOrigin = _bBaseOrigin;
        if ( _bWorldAxes != 0 )
        {
            value._frame._axisX = float3::UnitX;
            value._frame._axisY = float3::UnitY;
            value._frame._axisZ = float3::UnitZ;
        }
        if ( _kind == PropertyShapeKind::Box || _kind == PropertyShapeKind::Arrow )
        {
            const float3* pVector = _pProperty->getValuePtr<float3>( pInstance );
            value._vector         = pVector != nullptr ? *pVector : float3{};
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
} // namespace sw::editor
