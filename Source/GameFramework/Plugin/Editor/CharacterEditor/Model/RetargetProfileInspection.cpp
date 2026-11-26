#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileInspection.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"

namespace sw::editor
{
    namespace
    {
        struct RetargetProfileInspectionInternal
        {
            /** @brief 레퍼런스 포즈에서 이름 사슬의 마디 길이 합(m)입니다. 모르는 본은 건너뜁니다(묶기가 이미 오류로 거른다). */
            static float32 computeChainLength( const Skeleton& skeleton, const vector<hashed_string>& listBone )
            {
                Pose             reference;
                vector<float4x4> listModel;
                reference.setToReference( skeleton );
                reference.computeModelSpace( skeleton.getParentIndices(), listModel );
                float32          length = 0.0f;
                optional<uint32> previous{};
                for ( const hashed_string& name : listBone )
                {
                    // 없는 본은 `validateBone` 이 문제 줄로 알린다 — 길이는 있는 본만 잇는다.
                    const optional<uint32> bone = skeleton.findBoneIndex( name );
                    if ( bone.has_value() == false )
                        continue;
                    if ( previous.has_value() )
                        length += ( listModel[*bone].getTranslation() - listModel[*previous].getTranslation() ).getLength();
                    previous = bone;
                }
                return length;
            }

            /** @brief @p name 이 스켈레톤에 있는지 보고, 없으면 까닭을 한 줄 남깁니다. */
            static void validateBone( const Skeleton& skeleton, const hashed_string& name, string_view where, const utf8* pSide, vector<string>& outListProblem )
            {
                StringBuilder<constant::buffer::kMax256> line;
                if ( name.empty() )
                {
                    line.appendFormat( "%#: no %# bone is set", where, pSide );
                    outListProblem.push_back( string( line.view() ) );
                    return;
                }
                if ( skeleton.findBoneIndex( name ).has_value() )
                    return;
                line.appendFormat( "%#: the %# skeleton has no bone '%#'", where, pSide, name.c_str() );
                outListProblem.push_back( string( line.view() ) );
            }

            /** @brief 사슬 문제 한 줄(`Chain '<이름>': <내용>`)을 목록에 더합니다. */
            static void addChainLine( const RetargetChain& chain, const utf8* pText, vector<string>& outList )
            {
                StringBuilder<constant::buffer::kMax256> line;
                line.appendFormat( "Chain '%#': %#", chain._name.c_str(), pText );
                outList.push_back( string( line.view() ) );
            }

            /** @brief 런타임 묶기가 거절할 이름을 모두 모읍니다(리타기터는 첫 하나에서 멈추고 로그만 남긴다). */
            static void collectProblems( const RetargetProfile& profile, const Skeleton& source, const Skeleton& target, vector<string>& outListProblem )
            {
                validateBone( source, profile.getSourceRoot(), "Root", "source", outListProblem );
                validateBone( target, profile.getTargetRoot(), "Root", "target", outListProblem );
                validateBone( source, profile.getSourcePelvis(), "Pelvis", "source", outListProblem );
                validateBone( target, profile.getTargetPelvis(), "Pelvis", "target", outListProblem );
                for ( const RetargetChain& chain : profile.getChains() )
                {
                    fixed_string<constant::buffer::kMax128> label;
                    formatstring( label.data(), label.capacity(), "Chain '%#'", chain._name.c_str() );
                    if ( chain._listSourceBone.empty() )
                        addChainLine( chain, "no source bones", outListProblem );
                    if ( chain._listTargetBone.empty() )
                        addChainLine( chain, "no target bones", outListProblem );
                    for ( const hashed_string& name : chain._listSourceBone )
                    {
                        validateBone( source, name, label.view(), "source", outListProblem );
                    }
                    for ( const hashed_string& name : chain._listTargetBone )
                    {
                        validateBone( target, name, label.view(), "target", outListProblem );
                    }
                }
                for ( const RetargetBonePair& pair : profile.getBonePairs() )
                {
                    validateBone( source, pair._source, "Bone map", "source", outListProblem );
                    validateBone( target, pair._target, "Bone map", "target", outListProblem );
                }
                for ( const RetargetPoseBone& poseBone : profile.getTargetPose() )
                {
                    validateBone( target, poseBone._bone, "Retarget pose", "target", outListProblem );
                }
            }

            /** @brief @p ancestor 가 @p bone 의 조상이면 true 입니다. */
            static bool isAncestor( const Skeleton& skeleton, uint32 ancestor, uint32 bone )
            {
                const vector<int32>& listParent = skeleton.getParentIndices();
                for ( int32 current = listParent[bone]; current >= 0; current = listParent[static_cast<size_t>( current )] )
                {
                    if ( static_cast<uint32>( current ) == ancestor )
                        return true;
                }
                return false;
            }

            /** @brief 본 목록이 뿌리에서 끝으로 부모 줄을 따라가면 true 입니다. 뒤집어 적으면 묶이기는 해도 팔다리가 거꾸로 짝지어진다(언리얼은 사슬을 이어진 본으로만 받는다). */
            static bool isRootToEnd( const Skeleton& skeleton, const vector<hashed_string>& listBone )
            {
                for ( size_t index = 1; index < listBone.size(); ++index )
                {
                    // 없는 본은 `validateBone` 이 따로 알린다 — 그 사슬은 뿌리 → 끝 순서를 말할 수 없다.
                    const optional<uint32> ancestor = skeleton.findBoneIndex( listBone[index - 1] );
                    const optional<uint32> bone     = skeleton.findBoneIndex( listBone[index] );
                    if ( ancestor.has_value() == false || bone.has_value() == false || isAncestor( skeleton, *ancestor, *bone ) == false )
                        return false;
                }
                return true;
            }

            /** @brief 대상 본 하나를 짝 목록에 넣고, 이미 있으면 "앞 짝이 이긴다" 경고를 남깁니다(`PoseRetargeter::populatePairs` 가 말없이 버리는 줄). */
            static void claimTarget( const hashed_string& name, vector<hashed_string>& inoutListClaimed, vector<string>& outListWarning )
            {
                if ( VectorUtil::contains( inoutListClaimed, name ) == false )
                {
                    inoutListClaimed.push_back( name );
                    return;
                }
                StringBuilder<constant::buffer::kMax256> line;
                line.appendFormat( "Target bone '%#' is mapped more than once - the first mapping wins", name.c_str() );
                outListWarning.push_back( string( line.view() ) );
            }

            /** @brief 이름이 다 맞은 프로필에서 묶이지만 뜻대로 돌지 않을 곳을 모읍니다. */
            static void collectWarnings( const RetargetProfile& profile, const Skeleton& source, const Skeleton& target, vector<string>& outListWarning )
            {
                vector<hashed_string> listClaimed;
                claimTarget( profile.getTargetRoot(), listClaimed, outListWarning );
                claimTarget( profile.getTargetPelvis(), listClaimed, outListWarning );
                for ( const RetargetChain& chain : profile.getChains() )
                {
                    if ( isRootToEnd( source, chain._listSourceBone ) == false )
                        addChainLine( chain, "the source bones do not run from root to end", outListWarning );
                    if ( isRootToEnd( target, chain._listTargetBone ) == false )
                        addChainLine( chain, "the target bones do not run from root to end", outListWarning );
                    if ( chain._bIKGoal == SW_TRUE && chain._listTargetBone.size() < 2 )
                        addChainLine( chain, "an IK goal needs at least two target bones - it is ignored", outListWarning );
                    for ( const hashed_string& name : chain._listTargetBone )
                    {
                        claimTarget( name, listClaimed, outListWarning );
                    }
                }
                for ( const RetargetBonePair& pair : profile.getBonePairs() )
                {
                    claimTarget( pair._target, listClaimed, outListWarning );
                }
            }
        };
    } // namespace

    expected<void> RetargetProfileInspection::inspect( const RetargetProfile& profile, RetargetProfileInspection& outInspection )
    {
        // 스켈레톤은 런타임과 같은 공유 캐시의 한 벌을 읽는다(빈 경로는 읽지 않는다 — nullptr).
        const shared_ptr<const Skeleton> pSourceSkeleton = SharedAssetCache<Skeleton>::acquire( profile.getSourceSkeletonPath() ).snapshot();
        const shared_ptr<const Skeleton> pTargetSkeleton = SharedAssetCache<Skeleton>::acquire( profile.getTargetSkeletonPath() ).snapshot();
        return inspect( profile, pSourceSkeleton.get(), pTargetSkeleton.get(), outInspection );
    }

    expected<void> RetargetProfileInspection::inspect( const RetargetProfile& profile, const Skeleton* pSourceSkeleton, const Skeleton* pTargetSkeleton, RetargetProfileInspection& outInspection )
    {
        using Internal = RetargetProfileInspectionInternal;
        outInspection  = RetargetProfileInspection{};
        if ( pSourceSkeleton == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "The source skeleton could not be read", profile.getSourceSkeletonPath() } );
        if ( pTargetSkeleton == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "The target skeleton could not be read", profile.getTargetSkeletonPath() } );
        Internal::collectProblems( profile, *pSourceSkeleton, *pTargetSkeleton, outInspection._listProblem );
        if ( outInspection._listProblem.empty() == false )
            return unexpected( Error{ ErrorKind::NotFound, "The profile names a bone or field the skeletons do not have", outInspection._listProblem.front() } );
        // 런타임과 같은 검사 하나 — 묶이지 않으면 저장해도 쓸 수 없다. 이름은 위에서 다 봤으니 여기 오면 까닭은 리타기터의 로그에 있다.
        PoseRetargeter retargeter;
        if ( retargeter.initialize( profile, *pSourceSkeleton, *pTargetSkeleton, nullptr ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "The profile does not bind to the two skeletons - see the log" } );
        outInspection._heightRatio           = retargeter.getHeightRatio();
        outInspection._mappedTargetBoneCount = retargeter.getMappedBoneCount();
        outInspection._targetBoneCount       = pTargetSkeleton->getBoneCount();
        for ( const RetargetChain& chain : profile.getChains() )
        {
            RetargetChainInspection inspection{};
            inspection._name         = chain._name;
            inspection._sourceLength = Internal::computeChainLength( *pSourceSkeleton, chain._listSourceBone );
            inspection._targetLength = Internal::computeChainLength( *pTargetSkeleton, chain._listTargetBone );
            outInspection._listChain.push_back( inspection );
        }
        Internal::collectWarnings( profile, *pSourceSkeleton, *pTargetSkeleton, outInspection._listWarning );
        return {};
    }
} // namespace sw::editor
