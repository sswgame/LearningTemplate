/**
 * @file DeformableModifier.cpp
 * @brief 하위 스택을 품는 묶음 모디파이어를 구현합니다. 하위 스택 직렬화는 기본 클래스의 추가 상태 확장 지점(`writeExtraJSON` · `readExtraJSON`)을 쓴다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/DeformableModifier.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    DeformableModifier::DeformableModifier()
        : MeshModifier()
        , _stack{}
    {
    }

    DeformableModifier* DeformableModifier::findFrom( MeshModifier& modifier )
    {
        return modifier.getTypeID() == "deformable" ? static_cast<DeformableModifier*>( &modifier ) : nullptr;
    }

    void DeformableModifier::applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const
    {
        if ( _stack.getModifierCount() == 0 )
            return;
        // 하위 스택에게는 이 모디파이어 공간이 메시 로컬이다 — 로컬 → 월드 행렬을 모디파이어 → 월드로 바꿔 넘긴다
        MeshModifierEvaluation subEvaluation{};
        subEvaluation._meshToWorld = context._modifierToWorld;
        subEvaluation._pInputs     = context.getInputs();
        subEvaluation._time        = context.getTime();
        _stack.apply( inoutMesh, subEvaluation );
    }

    void DeformableModifier::writeExtraJSON( const JSONValue& object ) const
    {
        object.set( "stack" ).setString( _stack.makeJSONText( -1 ) );
    }

    bool DeformableModifier::readExtraJSON( const JSONValue& object )
    {
        _stack.clear();
        if ( object.has( "stack" ) == false )
            return true; // 하위 스택이 없는 문서 — 빈 묶음
        const JSONValue stackText = object.get( "stack" );
        if ( stackText.isString() == false )
            return false;
        return _stack.readJSONText( stackText.asString() );
    }

    SW_MESH_MODIFIER( DeformableModifier, "deformable", "Deformable", "Group" );
} // namespace sw::editor
