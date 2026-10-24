/**
 * @file WorldWarpModifier.cpp
 * @brief 월드 공간 워프 모디파이어를 구현합니다 — 공유 정의 파일의 스택을 워프 오브젝트의 공간에서 겁니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/WorldWarpModifier.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    WorldWarpModifier::WorldWarpModifier()
        : MeshModifier()
        , _warpStack{
    }
        , _loadedPath{}
        , _bLoadTried{ false }
        , _bLoaded{ false }
        , _fileParam{ addParam( MeshModifierParamDef{ "Warp File", MeshModifierParamKind::AssetPath, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Shared warp definition (modifier stack document)" }.withLayout( "Warp", "Warp definition file shared by every mesh bound to the same warp object" ).withAssetFilter( kFileExtension ) ) }
    {
        // 워프는 씬 오브젝트(공간 기준)의 자리에서 건다 — 메시 로컬로 시작하면 메시마다 다른 자리에서 휜다.
        setWorldSpace( true );
    }

    bool WorldWarpModifier::writeWarpFile( const MeshModifierStack& stack, string_view path )
    {
        return FileUtil::writeTextFile( path, stack.makeJSONText() );
    }

    bool WorldWarpModifier::readWarpFile( string_view path, MeshModifierStack& outStack )
    {
        outStack.clear();
        string text;
        if ( FileUtil::readTextFile( path, text ) == false )
            return false;
        return outStack.readJSONText( text );
    }

    void WorldWarpModifier::applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const
    {
        const string& path = getText( _fileParam );
        if ( path.empty() )
            return;
        const MeshModifierStack* pWarp = findWarp( path );
        if ( pWarp == nullptr || pWarp->getModifierCount() == 0 )
            return;
        // 하위 스택에게는 워프 공간(이 모디파이어의 공간 = 워프 오브젝트)이 메시 로컬이다 — `DeformableModifier` 와 같다.
        MeshModifierEvaluation subEvaluation{};
        subEvaluation._meshToWorld  = context._modifierToWorld;
        subEvaluation._pInputs      = context.getInputs();
        subEvaluation._time         = context.getTime();
        subEvaluation._pTaskManager = context.getTaskManager();
        pWarp->apply( inoutMesh, subEvaluation );
    }

    void WorldWarpModifier::onParamsChanged()
    {
        // 경로를 다시 고르면(같은 경로여도) 파일을 다시 읽는다 — 정의 파일을 고친 뒤 갱신하는 길이다.
        _bLoadTried = false;
    }

    const MeshModifierStack* WorldWarpModifier::findWarp( const string& path ) const
    {
        if ( _bLoadTried == false || _loadedPath != path )
        {
            _bLoadTried = true;
            _loadedPath = path;
            _bLoaded    = readWarpFile( path, _warpStack );
            if ( _bLoaded == false )
                SW_LOG_WARNING( "World warp file '%#' could not be read or is malformed - skipped", path.c_str() );
        }
        return _bLoaded ? &_warpStack : nullptr;
    }

    SW_MESH_MODIFIER( WorldWarpModifier, "worldwarp", "World Warp", "Group" );
} // namespace sw::editor
