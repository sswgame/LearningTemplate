#include "pch.h"

#include "Editor/Common/Commands/Asset/RigSaveParams.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Animation/Component/PoseModifierComponent.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw::editor
{
    namespace
    {
        struct RigFileRewriteInternal
        {
            /** @brief 디스크의 글을 쓰는 되돌리기 절차 하나입니다(되돌리기 · 다시 하기가 그 글을 다시 쓴다 — 파일 감시가 리그를 다시 읽는다). */
            static void writeRigText( const string& writePath, const string& text )
            {
                if ( FileUtil::writeTextFile( writePath, text ) == false )
                    SW_LOG_ERROR_ID( "RigDebugPanel.RigFileCouldNot", "Rig file '%#' could not be written", writePath.c_str() );
            }

            /** @brief @p list 에 `name` 이 @p name 인 줄이 있으면 true 입니다. */
            static bool hasNamedItem( const XMLNode& list, const hashed_string& name )
            {
                for ( XMLNode item = list.findChild(); item.isValid(); item = item.findNextSibling() )
                {
                    if ( hashed_string( string_view{ item.getAttributeText( "name" ) } ) == name )
                        return true;
                }
                return false;
            }
        };
    } // namespace

    bool RigSaveParams::rewriteSettings( string_view xmlText, string_view sourceLabel, const RigSaveParams& saveParams, string& outXMLText )
    {
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xmlText, sourceLabel, "RigAsset", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "RigFileRewrite.RigMalformedXMLRig", "Rig '%#': malformed XML, rig settings not saved", sourceLabel );
            return false;
        }
        // 변형(`_base`) 파일은 고친 대상 · 노드를 이름 열쇠의 덮어쓰기 줄로 더한다 — 물려받은 줄은 파일에 없고, 펼친 한 벌을 쓰면 부모가 끊긴다.
        const bool bVariant = DataInheritance::findBasePath( xmlText ).empty() == false;
        XMLNode    nodes    = root.findChild( "nodes" );
        if ( nodes.isValid() == false && bVariant )
            nodes = root.appendChild( "nodes" );
        if ( nodes.isValid() == false )
        {
            SW_LOG_ERROR_ID( "RigFileRewrite.RigNoNodesRig", "Rig '%#': no 'nodes', rig settings not saved", sourceLabel );
            return false;
        }
        XMLNode targets = root.findChild( "targets" );
        if ( targets.isValid() == false && bVariant && saveParams._listTarget.empty() == false )
            targets = root.appendChild( "targets" );
        for ( const RigTargetSaveParams& params : saveParams._listTarget )
        {
            bool       bFound    = false;
            const bool bOverride = bVariant && RigFileRewriteInternal::hasNamedItem( targets, params._name ) == false; // 물려받은 값을 덮으므로 모든 칸을 적는다
            if ( bOverride )
                targets.appendChild( "item" ).setAttribute( "name", params._name.view() ); // 물려받은 대상을 고친다 — 덮어쓰기 줄
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
                const bool   bWriteTranslation = bHasTranslation || bOverride || target.findAttribute( "translation" ) != nullptr;
                const bool   bWriteRotation    = bHasRotation || bOverride || target.findAttribute( "rotation" ) != nullptr;
                if ( bWriteTranslation )
                    target.setAttribute( "translation", string_view{ XMLReadUtil::makeFloatListText( &params._translation._x, 3 ) } );
                if ( bWriteRotation )
                    target.setAttribute( "rotation", string_view{ XMLReadUtil::makeFloatListText( &degrees._x, 3 ) } );
            }
            if ( bFound == false )
            {
                SW_LOG_ERROR_ID( "RigFileRewrite.RigTargetNotFile", "Rig '%#': target '%#' is not in the file, rig settings not saved", sourceLabel, params._name.c_str() );
                return false;
            }
        }
        for ( const RigNodeSaveParams& params : saveParams._listNode )
        {
            bool       bFound    = false;
            const bool bOverride = bVariant && RigFileRewriteInternal::hasNamedItem( nodes, params._name ) == false; // 물려받은 값을 덮으므로 모든 칸을 적는다
            if ( bOverride )
                nodes.appendChild( "item" ).setAttribute( "name", params._name.view() ); // 물려받은 노드를 고친다 — 덮어쓰기 줄
            for ( XMLNode node = nodes.findChild(); node.isValid(); node = node.findNextSibling() )
            {
                const bool bNamed = hashed_string( string_view{ node.getAttributeText( "name" ) } ) == params._name;
                if ( bNamed == false )
                    continue;
                bFound = true;
                node.setAttribute( "weight", params._weight );
                const bool bWriteEnabled = params._bEnabled == false || bOverride || node.findAttribute( "enabled" ) != nullptr;
                if ( bWriteEnabled )
                    node.setAttribute( "enabled", string_view{ params._bEnabled ? "true" : "false" } );
            }
            if ( bFound == false )
            {
                SW_LOG_ERROR_ID( "RigFileRewrite.RigNodeNotFile", "Rig '%#': node '%#' is not in the file, rig settings not saved", sourceLabel, params._name.c_str() );
                return false;
            }
        }
        const string text = document.saveToString();
        RigAsset     check;
        if ( check.loadFromXMLText( text, sourceLabel ) == false )
            return false; // 다시 읽기가 이유를 남겼다
        outXMLText = text;
        return true;
    }

    RigSaveParams RigSaveParams::makeFromNodes( const RigInstance& instance )
    {
        RigSaveParams params;
        for ( uint32 nodeIndex = 0; nodeIndex < instance.getNodeCount(); ++nodeIndex )
        {
            const RigNode& node = instance.getNode( nodeIndex );
            params._listNode.push_back( RigNodeSaveParams{ node.getName(), node.getWeight(), node.isEnabled() } );
        }
        return params;
    }

    string RigSaveParams::writeToRigFile( const PoseModifierComponent& modifier, const RigSaveParams& params, const utf8* pUndoLabel )
    {
        const string_view            rigPath  = modifier.getRigPath();
        const optional<AbsolutePath> readPath = EditorPaths::getAssetPaths().findLooseFile( rigPath );
        string                       text;
        if ( readPath.has_value() == false || FileUtil::readTextFile( readPath->str(), text ) == false )
            return string( "The rig file could not be read" );
        string rewritten;
        if ( rewriteSettings( text, rigPath, params, rewritten ) == false )
            return string( "The rig was not saved - see the log" );
        const optional<AbsolutePath> rigFile = EditorPaths::getAssetPaths().toDiskPath( rigPath );
        if ( rigFile.has_value() == false )
            return string( "The rig file could not be written" );
        const string& writePath = rigFile->str();
        if ( FileUtil::writeTextFile( writePath, rewritten ) == false )
            return string( "The rig file could not be written" );
        if ( pUndoLabel != nullptr )
        {
            EditorTransaction::push( SW_UNDO_STEP( RigFileRewriteInternal::writeRigText, writePath, text ),
                                     SW_UNDO_STEP( RigFileRewriteInternal::writeRigText, writePath, rewritten ), pUndoLabel );
        }
        SW_LOG_INFO_ID( "RigDebugPanel.RigSettingsSaved", "Rig settings saved to %#", writePath.c_str() );
        return string( kSavedStatus );
    }
} // namespace sw::editor
