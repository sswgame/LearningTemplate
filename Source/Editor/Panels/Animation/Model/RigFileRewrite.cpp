#include "pch.h"

#include "Editor/Panels/Animation/Model/RigFileRewrite.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw::editor
{
    SW_LOG_CALLER( "RigFileRewrite" );

    bool RigFileRewrite::rewriteSettings( string_view xmlText, string_view sourceLabel, const RigSaveParams& saveParams, string& outXMLText )
    {
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xmlText, sourceLabel, "RigAsset", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR( "Rig '%#': malformed XML, rig settings not saved", sourceLabel );
            return false;
        }
        const XMLNode nodes = root.findChild( "nodes" );
        if ( nodes.isValid() == false )
        {
            SW_LOG_ERROR( "Rig '%#': no 'nodes', rig settings not saved", sourceLabel );
            return false;
        }
        const XMLNode targets = root.findChild( "targets" );
        for ( const RigTargetSaveParams& params : saveParams._listTarget )
        {
            bool bFound = false;
            for ( XMLNode target = targets.findChild(); target.isValid(); target = target.findNextSibling() )
            {
                const bool bNamed = hashed_string( string_view{ target.getAttributeText( "name" ) } ) == params._name;
                if ( bNamed == false )
                    continue;
                bFound = true;
                // 읽기(`readFloat3` · `readRotationDegrees`)와 같은 배치로 쓴다 — 회전은 도 단위 [피치, 요, 롤].
                const float3 degrees           = params._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
                const bool   bHasTranslation   = params._translation.getLengthSquared() > 0.0f;
                const bool   bHasRotation      = degrees.getLengthSquared() > 0.0f;
                const bool   bWriteTranslation = bHasTranslation || target.findAttribute( "translation" ) != nullptr;
                const bool   bWriteRotation    = bHasRotation || target.findAttribute( "rotation" ) != nullptr;
                if ( bWriteTranslation )
                    target.setAttribute( "translation", string_view{ XMLReadUtil::makeFloatListText( &params._translation._x, 3 ) } );
                if ( bWriteRotation )
                    target.setAttribute( "rotation", string_view{ XMLReadUtil::makeFloatListText( &degrees._x, 3 ) } );
            }
            if ( bFound == false )
            {
                SW_LOG_ERROR( "Rig '%#': target '%#' is not in the file, rig settings not saved", sourceLabel, params._name.c_str() );
                return false;
            }
        }
        for ( const RigNodeSaveParams& params : saveParams._listNode )
        {
            bool bFound = false;
            for ( XMLNode node = nodes.findChild(); node.isValid(); node = node.findNextSibling() )
            {
                const bool bNamed = hashed_string( string_view{ node.getAttributeText( "name" ) } ) == params._name;
                if ( bNamed == false )
                    continue;
                bFound = true;
                node.setAttribute( "weight", params._weight );
                const bool bWriteEnabled = params._bEnabled == false || node.findAttribute( "enabled" ) != nullptr;
                if ( bWriteEnabled )
                    node.setAttribute( "enabled", string_view{ params._bEnabled ? "true" : "false" } );
            }
            if ( bFound == false )
            {
                SW_LOG_ERROR( "Rig '%#': node '%#' is not in the file, rig settings not saved", sourceLabel, params._name.c_str() );
                return false;
            }
        }
        const string text = document.saveToString();
        RigAsset     check;
        if ( check.parseXML( text, sourceLabel ) == false )
            return false; // 다시 읽기가 이유를 남겼다
        outXMLText = text;
        return true;
    }
} // namespace sw::editor
