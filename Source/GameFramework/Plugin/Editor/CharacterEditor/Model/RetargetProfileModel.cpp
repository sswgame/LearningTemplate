// 리타깃 프로파일 창의 문서 Model(편집 연산 · 알림)입니다.
#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileModel.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

namespace sw::editor
{
    void RetargetProfileModel::recordDocumentEdit( string_view label ) const
    {
        SW_ASSERT( _pEditRecorder != nullptr ); // 문서 패널이 만들 때 건다(`bindDocumentEditRecorder`)
        _pEditRecorder->notifyEdited( label );
    }

    void RetargetProfileModel::setSkeletonPaths( string_view sourcePath, string_view targetPath )
    {
        _profile.setSkeletonPaths( sourcePath, targetPath );
        recordDocumentEdit( "Retarget skeletons" );
    }

    void RetargetProfileModel::setRootAndPelvis( const hashed_string& sourceRoot, const hashed_string& targetRoot, const hashed_string& sourcePelvis, const hashed_string& targetPelvis )
    {
        _profile.setRootAndPelvis( sourceRoot, targetRoot, sourcePelvis, targetPelvis, _profile.getTranslationMode() );
        recordDocumentEdit( "Retarget root and pelvis" );
    }

    void RetargetProfileModel::setTranslationMode( RetargetTranslationMode mode )
    {
        _profile.setRootAndPelvis( _profile.getSourceRoot(), _profile.getTargetRoot(), _profile.getSourcePelvis(), _profile.getTargetPelvis(), mode );
        recordDocumentEdit( "Retarget translation" );
    }

    void RetargetProfileModel::setMapSameNames( bool bMap )
    {
        _profile.setMapSameNames( bMap );
        recordDocumentEdit( "Retarget same names" );
    }

    void RetargetProfileModel::setHeightRatioOverride( float32 ratio )
    {
        _profile.setHeightRatioOverride( ratio );
        recordDocumentEdit( "Retarget height ratio" );
    }

    void RetargetProfileModel::setChains( vector<RetargetChain>&& listChain )
    {
        _profile.setChains( std::move( listChain ) );
        recordDocumentEdit( "Retarget chains" );
    }

    void RetargetProfileModel::setTargetPose( vector<RetargetPoseBone>&& listPose )
    {
        _profile.setTargetPose( std::move( listPose ) );
        recordDocumentEdit( "Retarget pose" );
    }

    void RetargetProfileModel::setBonePairs( vector<RetargetBonePair>&& listPair )
    {
        _profile.setBonePairs( std::move( listPair ) );
        recordDocumentEdit( "Retarget bone map" );
    }
} // namespace sw::editor
