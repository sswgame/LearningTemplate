/**
 * @file SyntheticRigTestUtil.h
 * @brief 합성 리그(`game/empty/rigs_raw/` 의 `.glb` + `.rig.json`, `Scripts/dev/MakeSyntheticRig.py --representatives`)를 임포터로 읽어 런타임 자산(스켈레톤 · 스킨 메시 · 클립 폴더 · 리그)으로 만듭니다.
 * @details 임포트 테스트(`SyntheticRigImportTest`)와 비용 벤치(`SyntheticRigBenchTest`)가 함께 쓴다. 목록은 폴더의 파일이 정한다 — 대표를 더하면 두 테스트가 같이 받는다.
 */
#pragma once
#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImporter.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>

namespace test
{
    /**
     * @struct SyntheticRig
     * @brief 대표 합성 리그 하나를 임포트한 결과와, 캐릭터가 바로 쓸 런타임 자산입니다.
     */
    struct SyntheticRig
    {
        sw::string                         _stem;       ///< 파일 줄기(`biped_b150`)
        sw::editor::ModelImportResult      _import;     ///< 임포터 결과 그대로
        sw::shared_ptr<sw::Skeleton>       _skeleton;   ///< 캐릭터가 나누는 스켈레톤
        sw::shared_ptr<sw::Mesh>           _mesh;       ///< 스킨 · 모프를 포함한 원본 메시
        sw::string                         _clipFolder; ///< 클립을 쓴 임시 폴더(`<클립 이름>.animclip`)
        sw::shared_ptr<const sw::RigAsset> _rig;        ///< 옆 `.rig.json`
    };

    /** @brief 대표 합성 리그 폴더의 리소스 경로입니다(`MakeSyntheticRig.kRepresentativeFolder`). */
    inline constexpr const utf8* kSyntheticRigFolder = "game/empty/rigs_raw";

    /** @brief 폴더의 대표 `.glb` 경로(절대)를 이름 순서로 모읍니다. */
    inline sw::vector<sw::string> collectSyntheticRigPaths()
    {
        sw::vector<sw::string> listPath;
        if ( sw::FileUtil::collectFiles( sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), kSyntheticRigFolder ), ".glb", listPath, false ) == false )
            listPath.clear();
        std::sort( listPath.begin(), listPath.end() );
        return listPath;
    }

    /**
     * @brief `.glb` 하나를 임포트해 런타임 자산을 만듭니다. 클립은 임시 폴더에 쓰고(애니메이터가 폴더에서 읽는다), 리그는 옆 `.rig.json` 을 읽습니다.
     * @return 임포트 · 클립 쓰기 · 리그 읽기 가운데 하나라도 실패하면 false 입니다.
     */
    [[nodiscard]] inline bool loadSyntheticRig( const sw::string& glbPath, SyntheticRig& outRig )
    {
        const sw::string fileName = sw::FileUtil::getFileNamePart( glbPath );
        outRig._stem              = fileName.substr( 0, fileName.size() - 4 ); // ".glb"
        if ( sw::editor::ModelImporter::readModelAsset( glbPath, sw::editor::ModelImportRule{}, outRig._import ) == false )
            return false;
        outRig._skeleton = sw::make_shared<sw::Skeleton>( outRig._import._skeleton );
        outRig._mesh     = sw::Mesh::create();
        outRig._mesh->setVertices( outRig._import._mesh._listVertex );
        outRig._mesh->setSkin( outRig._import._mesh._listSkinVertex, outRig._import._mesh._skinBoneCount );
        outRig._mesh->setMorphTargets( outRig._import._mesh._listMorphTarget );
        outRig._clipFolder = test::makeTempDirectory( outRig._stem.c_str() );
        for ( const sw::editor::ModelImportClip& imported : outRig._import._listClip )
        {
            const sw::string clipPath = sw::FileUtil::joinPath( outRig._clipFolder, imported._fileStem + sw::string( sw::AnimationClip::kExtension ) );
            if ( imported._clip.saveToFile( clipPath ) == false )
                return false;
        }
        sw::shared_ptr<sw::RigAsset> rig = sw::make_shared<sw::RigAsset>();
        if ( rig->loadFromResource( sw::string( kSyntheticRigFolder ) + "/" + outRig._stem + ".rig.json" ) == false )
            return false;
        outRig._rig = rig;
        return true;
    }
} // namespace test
