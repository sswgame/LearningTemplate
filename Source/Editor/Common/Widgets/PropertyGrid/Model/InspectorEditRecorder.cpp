#include "pch.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorEditRecorder.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"

#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 구간 표
    // ------------------------------------------------------------------------------
    void PropertyEditSpanSet::open( uint64 key, PropertyEditSpan&& span )
    {
        closeHeld();
        _held         = std::move( span );
        _heldKey      = key;
        _bHasHeld     = SW_TRUE;
        _bHeldTouched = SW_FALSE;
    }

    void PropertyEditSpanSet::touch( uint64 key )
    {
        if ( isHeld( key ) )
            _bHeldTouched = SW_TRUE;
    }

    void PropertyEditSpanSet::collectClosed( vector<PropertyEditSpan>& outListClosed )
    {
        // 이번 프레임에 위젯이 활성이 아니었다 — 풀렸거나(놓은 프레임의 쓰기까지 담았다), 사라졌거나(해제를 받을 수 없다), 한 번 쓰기였다.
        if ( _bHasHeld != SW_FALSE && _bHeldTouched == SW_FALSE )
            closeHeld();
        _bHeldTouched = SW_FALSE;
        outListClosed = std::move( _listClosed );
        _listClosed.clear();
    }

    void PropertyEditSpanSet::clear()
    {
        _held         = PropertyEditSpan{};
        _heldKey      = 0;
        _bHasHeld     = SW_FALSE;
        _bHeldTouched = SW_FALSE;
        _listClosed.clear();
    }

    void PropertyEditSpanSet::closeHeld()
    {
        if ( _bHasHeld == SW_FALSE )
            return;
        _listClosed.push_back( std::move( _held ) );
        _held     = PropertyEditSpan{};
        _heldKey  = 0;
        _bHasHeld = SW_FALSE;
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
                span._listTarget.push_back( PropertyEditTarget::make( owner, pComponent ) );
                span._listPath.push_back( string{ prop._name.c_str() } );
                span._listBefore.push_back( SerializerUtil::formatPropertyText( prop, ObjectAddress{ pInstance }, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
            }

            /** @brief 단계 하나의 값 글 표 — 같은 글은 한 벌만 든다(다중 선택은 대개 값이 하나다). */
            struct TextTable
            {
                vector<string>                _listText;
                vector<uint32>                _listIndex;
                unordered_map<string, uint32> _mapIndex;

                void add( const string& text )
                {
                    const auto [iter, bInserted] = _mapIndex.try_emplace( text, static_cast<uint32>( _listText.size() ) );
                    if ( bInserted )
                        _listText.push_back( text );
                    _listIndex.push_back( iter->second );
                }
            };

            /** @brief 구간 @p span 의 바뀐 대상 가운데 경로가 @p path 인 것을 한 명령으로 거래 @p transaction 에 남깁니다. */
            static void recordPath( EditorTransaction& transaction, const PropertyEditSpan& span, const vector<size_t>& listChanged, const vector<string>& listAfter,
                                    const string& path )
            {
                vector<PropertyEditTarget> listTarget;
                TextTable                  before;
                TextTable                  after;
                GameObject*                pRecordOwner = nullptr;
                for ( size_t changed = 0; changed < listChanged.size(); ++changed )
                {
                    const size_t index = listChanged[changed];
                    if ( span._listPath[index] != path )
                        continue;
                    listTarget.push_back( span._listTarget[index] );
                    before.add( span._listBefore[index] );
                    after.add( listAfter[changed] );
                    if ( pRecordOwner == nullptr )
                        pRecordOwner = PropertyEditTarget::findObject( span._sceneKey, span._listTarget[index]._objectID );
                }
                if ( pRecordOwner == nullptr )
                    return;
                CommandStack::Command command{};
                command._label = span._label;
                command._undo  = SW_UNDO_STEP( PropertyEditTarget::applyPropertyText, span._sceneKey, path, listTarget, before._listText, before._listIndex );
                command._redo  = SW_UNDO_STEP( PropertyEditTarget::applyPropertyText, span._sceneKey, path, listTarget, after._listText, after._listIndex );
                transaction.recordObjectCommand( *pRecordOwner, std::move( command ) );
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
        span._listPath.reserve( listComponent.size() );
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

    void PropertyEditSpan::commit( EditorTransaction& transaction, vector<PropertyEditSpan>& inoutListSpan )
    {
        if ( inoutListSpan.empty() )
            return;
        if ( EditorPlaySession::getUIState().areSceneEditsAllowed() == false )
        {
            inoutListSpan.clear();
            return;
        }
        for ( const PropertyEditSpan& span : inoutListSpan )
        {
            vector<size_t> listChanged;
            vector<string> listAfter;
            vector<string> listPath; // 바뀐 대상의 경로(겹치지 않게) — 확장 영역만 여럿이다
            string         afterText;
            for ( size_t index = 0; index < span._listTarget.size(); ++index )
            {
                // 그 사이 사라진 대상(지웠거나 미리보기 창을 닫았다)은 빠진다 — 되돌릴 곳이 없다.
                GameObject* pObject = PropertyEditTarget::findObject( span._sceneKey, span._listTarget[index]._objectID );
                if ( pObject == nullptr || span._listTarget[index].readValueText( *pObject, span._listPath[index], afterText ) == false || afterText == span._listBefore[index] )
                    continue;
                listChanged.push_back( index );
                listAfter.push_back( std::move( afterText ) );
                afterText = string{};
                VectorUtil::addUnique( listPath, span._listPath[index] );
            }
            for ( const string& path : listPath )
                InspectorEditRecorderInternal::recordPath( transaction, span, listChanged, listAfter, path );
        }
        inoutListSpan.clear();
    }
} // namespace sw::editor
