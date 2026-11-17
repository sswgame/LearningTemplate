#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorEditRecorder.h"

#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 구간 표
    // ------------------------------------------------------------------------------
    void PropertyEditSpanSet::trackWidget( uint32 widgetID, bool bActive, bool bDeactivated )
    {
        const bool bArmed  = bActive || bDeactivated;
        _armedWidgetID     = bArmed ? widgetID : 0;
        _bArmedDeactivated = bDeactivated ? SW_TRUE : SW_FALSE;
        if ( bArmed && isHeld( widgetID ) )
        {
            _bHeldTouched = SW_TRUE;
            if ( bDeactivated )
                _bHeldClosing = SW_TRUE;
        }
    }

    bool PropertyEditSpanSet::continueHeld( string_view propertyPath )
    {
        if ( _armedWidgetID == 0 || isHeld( _armedWidgetID ) == false || _held._listTarget.empty() || string_view{ _held._listTarget[0]._propertyPath } != propertyPath )
            return false;
        _armedWidgetID = 0;
        return true;
    }

    void PropertyEditSpanSet::open( PropertyEditSpan&& span )
    {
        if ( _armedWidgetID == 0 )
        {
            span._widgetID = 0;
            _listClosed.push_back( std::move( span ) );
            return;
        }
        closeHeld();
        span._widgetID = _armedWidgetID;
        _held          = std::move( span );
        _bHasHeld      = SW_TRUE;
        _bHeldTouched  = SW_TRUE;
        _bHeldClosing  = _bArmedDeactivated;
        _armedWidgetID = 0;
    }

    void PropertyEditSpanSet::openForWidget( uint32 widgetID, PropertyEditSpan&& span )
    {
        if ( widgetID == 0 || isHeld( widgetID ) )
            return;
        closeHeld();
        span._widgetID = widgetID;
        _held          = std::move( span );
        _bHasHeld      = SW_TRUE;
        _bHeldTouched  = SW_TRUE;
        _bHeldClosing  = SW_FALSE;
    }

    void PropertyEditSpanSet::collectClosed( vector<PropertyEditSpan>& outListClosed )
    {
        // 풀린 위젯, 그리고 이번 프레임에 그리지 않은 위젯(선택이 바뀌었거나 머리를 접었다 — 해제를 받을 수 없다)의 구간을 닫는다.
        if ( _bHasHeld != SW_FALSE && ( _bHeldClosing != SW_FALSE || _bHeldTouched == SW_FALSE ) )
            closeHeld();
        _bHeldTouched  = SW_FALSE;
        _armedWidgetID = 0;
        outListClosed  = std::move( _listClosed );
        _listClosed.clear();
    }

    void PropertyEditSpanSet::clear()
    {
        _held          = PropertyEditSpan{};
        _bHasHeld      = SW_FALSE;
        _bHeldTouched  = SW_FALSE;
        _bHeldClosing  = SW_FALSE;
        _armedWidgetID = 0;
        _listClosed.clear();
    }

    void PropertyEditSpanSet::closeHeld()
    {
        if ( _bHasHeld == SW_FALSE )
            return;
        _listClosed.push_back( std::move( _held ) );
        _held         = PropertyEditSpan{};
        _bHasHeld     = SW_FALSE;
        _bHeldClosing = SW_FALSE;
    }

    // ------------------------------------------------------------------------------
    // 찍기 · 기록
    // ------------------------------------------------------------------------------
    namespace
    {
        struct InspectorEditRecorderInternal
        {
            /** @brief 컴포넌트 @p pComponent(nullptr 이면 @p owner 자신)의 @p prop 을 구간 대상으로 더합니다. */
            static void appendTarget( PropertyEditSpan& span, const GameObject& owner, const Component* pComponent, const PropertyInfo& prop )
            {
                const void* pInstance = pComponent != nullptr ? static_cast<const void*>( pComponent ) : static_cast<const void*>( &owner );
                span._listTarget.push_back( PropertyEditTarget::make( owner, pComponent, prop ) );
                span._listBefore.push_back( SerializerUtil::formatPropertyText( prop, ObjectAddress{ pInstance }, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
            }
        };
    } // namespace

    PropertyEditSpan PropertyEditSpan::captureProperty( const GameObject& owner, const Component* pComponent, const PropertyInfo& prop,
                                                        const Delegate<void( vector<Component*>& )>& collectEditTargets, string_view label )
    {
        PropertyEditSpan span;
        span._sceneKey = PropertyEditTarget::findSceneKey( owner );
        span._label    = string{ label };
        vector<Component*> listComponent;
        if ( collectEditTargets.isBound() )
            collectEditTargets( listComponent );
        if ( listComponent.empty() )
        {
            InspectorEditRecorderInternal::appendTarget( span, owner, pComponent, prop );
            return span;
        }
        span._listTarget.reserve( listComponent.size() );
        span._listBefore.reserve( listComponent.size() );
        for ( const Component* pEdited : listComponent )
        {
            if ( pEdited != nullptr && pEdited->getOwner() != nullptr )
                InspectorEditRecorderInternal::appendTarget( span, *pEdited->getOwner(), pEdited, prop );
        }
        return span;
    }

    PropertyEditSpan PropertyEditSpan::captureComponent( const Component& component, string_view label )
    {
        PropertyEditSpan span;
        span._label              = string{ label };
        const GameObject* pOwner = component.getOwner();
        const TypeInfo*   pType  = component.getTypeInfo();
        if ( pOwner == nullptr || pType == nullptr )
            return span;
        span._sceneKey = PropertyEditTarget::findSceneKey( *pOwner );
        for ( const PropertyInfo& prop : pType->getPropertiesWithBase() )
        {
            InspectorEditRecorderInternal::appendTarget( span, *pOwner, &component, prop );
        }
        return span;
    }

    void PropertyEditSpan::commit( vector<PropertyEditSpan>& inoutListSpan )
    {
        if ( inoutListSpan.empty() )
            return;
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
        {
            inoutListSpan.clear();
            return;
        }
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        for ( const PropertyEditSpan& span : inoutListSpan )
        {
            vector<PropertyEditTarget> listTarget;
            vector<string>             listBefore;
            vector<string>             listAfter;
            GameObject*                pRecordOwner = nullptr;
            string                     afterText;
            for ( size_t index = 0; index < span._listTarget.size(); ++index )
            {
                // 그 사이 사라진 대상(지웠거나 미리보기 창을 닫았다)은 빠진다 — 되돌릴 곳이 없다.
                GameObject* pObject = PropertyEditTarget::findObject( span._sceneKey, span._listTarget[index]._objectID );
                if ( pObject == nullptr || span._listTarget[index].readValueText( *pObject, afterText ) == false || afterText == span._listBefore[index] )
                    continue;
                listTarget.push_back( span._listTarget[index] );
                listBefore.push_back( span._listBefore[index] );
                listAfter.push_back( std::move( afterText ) );
                afterText = string{};
                if ( pRecordOwner == nullptr )
                    pRecordOwner = pObject;
            }
            if ( pRecordOwner == nullptr )
                continue;
            CommandStack::Command command{};
            command._label = span._label;
            command._undo  = SW_UNDO_STEP( PropertyEditTarget::applyPropertyText, span._sceneKey, listTarget, listBefore );
            command._redo  = SW_UNDO_STEP( PropertyEditTarget::applyPropertyText, span._sceneKey, listTarget, listAfter );
            transaction.recordObjectCommand( *pRecordOwner, std::move( command ) );
        }
        inoutListSpan.clear();
    }
} // namespace sw::editor
