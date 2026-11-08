#include "pch.h"

#include "Engine/Animation/Skeletal/Skeleton.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct SkeletonInternal
        {
            /** @brief `_firstAddedBone` 의 "붙인 본 없음" 입니다. */
            static constexpr uint32 kNoAddedBone = 0xFFFFFFFFu;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SkeletonVariant::SkeletonVariant()
        : _base{}
        , _listAddedBone{}
        , _listBoneOverride{}
        , _listSocket{}
        , _listAttachment{}
        , _pelvisBone{}
        , _bPartial{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    string SkeletonVariant::toXML() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    SkeletonClipRule::SkeletonClipRule()
        : _sourceTranslation{}
        , _translation{}
        , _rotation{ quaternion::Identity }
        , _scale{ 1.0f, 1.0f, 1.0f }
        , _translationMode{ RetargetTranslationMode::Copy }
        , _bActive{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SkeletonVariantState::SkeletonVariantState()
        : _listClipRule{}
        , _listBasePath{}
        , _firstAddedBone{ SkeletonInternal::kNoAddedBone }
        , _translationRatio{ 1.0f }
    {
    }

    Skeleton::Skeleton()
        : _listBone{}
        , _listParentIndex{}
        , _listHierarchyHash{}
        , _listAttachment{}
        , _listSocket{}
        , _listVariantState{}
        , _listClipRule{}
        , _listBasePath{}
        , _firstAddedBone{ SkeletonInternal::kNoAddedBone }
        , _translationRatio{ 1.0f }
        , _bPartial{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    int32 Skeleton::addBone( const hashed_string& name, int32 parentIndex, const BoneTransform& referencePose )
    {
        // 부모가 자식보다 뒤에 있으면 모델 공간을 한 번 훑어서 구할 수 없다. 읽을 때 걸러 낸다.
        const int32 boneCount = static_cast<int32>( _listBone.size() );
        if ( parentIndex < constant::invalid_index::kInt32 || boneCount <= parentIndex )
        {
            SW_LOG_ERROR_ID( "Skeleton.BoneRejectedParentIndex", "Bone '%#' rejected: parent index %# is not an already-added bone (bone count %#)", name.c_str(), parentIndex, boneCount );
            return constant::invalid_index::kInt32;
        }

        SkeletonBone bone{};
        bone._name          = name;
        bone._parentIndex   = parentIndex;
        bone._referencePose = referencePose;
        _listBone.push_back( bone );
        _listParentIndex.push_back( parentIndex );
        const string_view nameText = name.c_str();
        const uint64      nameHash = HashUtil::appendBytes64( _listHierarchyHash.empty() ? HashUtil::kFNVOffset64 : _listHierarchyHash.back(), nameText.data(), nameText.size() );
        _listHierarchyHash.push_back( HashUtil::appendBytes64( nameHash, &parentIndex, sizeof( parentIndex ) ) );
        return boneCount;
    }

    void Skeleton::computeReferenceInverseBinds( vector<float4x4>& outListInverseBind ) const
    {
        Pose referencePose;
        referencePose.setToReference( *this );
        referencePose.computeModelSpace( _listParentIndex, outListInverseBind );
        for ( float4x4& model : outListInverseBind )
        {
            model = model.invert();
        }
    }

    uint64 Skeleton::getHierarchyHash( uint32 boneCount ) const
    {
        const bool bInRange = 0 < boneCount && boneCount <= _listHierarchyHash.size();
        return bInRange ? _listHierarchyHash[boneCount - 1] : 0u;
    }

    bool Skeleton::canDrawSkinBinding( const MeshSkinBinding& skinBinding ) const
    {
        const uint32 boneCount = skinBinding.getBoneCount();
        if ( boneCount == 0 || boneCount > getBoneCount() )
            return false;
        return skinBinding._layoutHash == MeshSkinBinding::kAnyLayout || skinBinding._layoutHash == getHierarchyHash( boneCount );
    }

    optional<uint32> Skeleton::findBoneIndex( const hashed_string& name ) const
    {
        for ( size_t boneIndex = 0; boneIndex < _listBone.size(); ++boneIndex )
        {
            if ( _listBone[boneIndex]._name == name )
                return static_cast<uint32>( boneIndex );
        }
        return nullopt;
    }

    void Skeleton::clear()
    {
        _listBone.clear();
        _listParentIndex.clear();
        _listHierarchyHash.clear();
        _listAttachment.clear();
        _listSocket.clear();
        _listVariantState.clear();
        _listClipRule.clear();
        _listBasePath.clear();
        _firstAddedBone   = SkeletonInternal::kNoAddedBone;
        _translationRatio = 1.0f;
        _bPartial         = SW_FALSE;
    }

    const SkeletonSocket* Skeleton::findSocket( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&SkeletonSocket::_name>( _listSocket, name );
    }

    BoneTransform Skeleton::applyClipRule( uint32 boneIndex, const BoneTransform& sampled ) const
    {
        if ( boneIndex >= _listClipRule.size() || _listClipRule[boneIndex]._bActive == SW_FALSE )
            return sampled;
        const SkeletonClipRule& rule   = _listClipRule[boneIndex];
        BoneTransform           result = sampled;
        switch ( rule._translationMode )
        {
            case RetargetTranslationMode::None:
                result._translation = rule._translation;
                break;
            case RetargetTranslationMode::ScaleByPelvisHeight:
                result._translation = rule._translation + ( sampled._translation - rule._sourceTranslation ) * _translationRatio;
                break;
            case RetargetTranslationMode::Copy:
                break;
        }
        result._rotation = ( rule._rotation * sampled._rotation ).normalize();
        result._scale    = float3::multiply( sampled._scale, rule._scale );
        return result;
    }

    bool Skeleton::addSocket( const SkeletonSocket& socket, string_view sourceLabel )
    {
        if ( findSocket( socket._name ) != nullptr )
        {
            SW_LOG_ERROR_ID( "Skeleton.SocketDuplicate", "Skeleton '%#': socket '%#' is defined twice", sourceLabel, socket._name.c_str() );
            return false;
        }
        if ( findBoneIndex( socket._parentBone ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "Skeleton.SocketNamesUnknownBone", "Skeleton '%#': socket '%#' names unknown bone '%#'", sourceLabel, socket._name.c_str(), socket._parentBone.c_str() );
            return false;
        }
        _listSocket.push_back( socket );
        return true;
    }

    bool Skeleton::applyBoneOverride( const SkeletonBoneOverride& boneOverride, string_view sourceLabel )
    {
        const optional<uint32> boneIndex = findBoneIndex( boneOverride._bone );
        if ( boneIndex.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Skeleton.VariantOverrideUnknownBone", "Skeleton variant '%#': bone override names unknown bone '%#'", sourceLabel, boneOverride._bone.c_str() );
            return false;
        }
        _listClipRule.resize( _listBone.size() );
        SkeletonClipRule& rule = _listClipRule[*boneIndex];
        BoneTransform&    pose = _listBone[*boneIndex]._referencePose;
        // 변형의 변형이면 클립의 기준은 맨 위 스켈레톤 그대로이고 나머지 차이가 쌓인다.
        if ( rule._bActive == SW_FALSE )
            rule._sourceTranslation = pose._translation;
        pose._translation     = pose._translation * boneOverride._translationScale + boneOverride._translationOffset;
        pose._rotation        = ( boneOverride._rotation * pose._rotation ).normalize();
        pose._scale           = float3::multiply( pose._scale, boneOverride._scale );
        rule._translation     = pose._translation;
        rule._rotation        = ( boneOverride._rotation * rule._rotation ).normalize();
        rule._scale           = float3::multiply( rule._scale, boneOverride._scale );
        rule._translationMode = boneOverride._translationMode;
        rule._bActive         = SW_TRUE;
        return true;
    }

    bool Skeleton::applyVariant( const SkeletonVariant& variant, const Skeleton& base, string_view sourceLabel )
    {
        *this = base;
        _listBasePath.insert( _listBasePath.begin(), variant._base );
        _bPartial = ( base._bPartial == SW_TRUE || variant._bPartial == SW_TRUE ) ? SW_TRUE : SW_FALSE;

        bool bValid = true;
        for ( const SkeletonBoneOverride& boneOverride : variant._listBoneOverride )
        {
            bValid = applyBoneOverride( boneOverride, sourceLabel ) && bValid;
        }
        const bool bScalesByPelvis = VectorUtil::containsIf( variant._listBoneOverride, []( const SkeletonBoneOverride& boneOverride )
        { return boneOverride._translationMode == RetargetTranslationMode::ScaleByPelvisHeight; } );
        if ( bValid && bScalesByPelvis )
        {
            // 골반 높이 비 — 리타깃(`PoseRetargeter`)과 같은 값이다. 변형의 변형은 부모의 비에 곱해 맨 위 기준을 지킨다.
            const optional<uint32> pelvisBone = findBoneIndex( variant._pelvisBone );
            vector<float4x4>       listBaseModel;
            vector<float4x4>       listModel;
            Pose                   pose;
            pose.setToReference( base );
            pose.computeModelSpace( base.getParentIndices(), listBaseModel );
            pose.setToReference( *this );
            pose.computeModelSpace( getParentIndices(), listModel );
            const bool bPelvisValid = pelvisBone.has_value() && MathUtil::abs( listBaseModel[*pelvisBone].getTranslation()._y ) > MathUtil::kEpsilon;
            if ( bPelvisValid == false )
            {
                SW_LOG_ERROR_ID( "Skeleton.VariantPelvisInvalid", "Skeleton variant '%#': ScaleByPelvisHeight needs _pelvisBone, a bone above the origin in the base (got '%#')", sourceLabel, variant._pelvisBone.c_str() );
                bValid = false;
            }
            else
            {
                _translationRatio = base._translationRatio * listModel[*pelvisBone].getTranslation()._y / listBaseModel[*pelvisBone].getTranslation()._y;
            }
        }
        for ( const SkeletonAddedBone& added : variant._listAddedBone )
        {
            const optional<uint32> parentBone = findBoneIndex( added._parentBone );
            if ( findBoneIndex( added._name ).has_value() || parentBone.has_value() == false )
            {
                SW_LOG_ERROR_ID( "Skeleton.VariantAddedBoneInvalid", "Skeleton variant '%#': added bone '%#' already exists or its parent '%#' does not", sourceLabel, added._name.c_str(), added._parentBone.c_str() );
                bValid = false;
                continue;
            }
            _firstAddedBone = MathUtil::min( _firstAddedBone, getBoneCount() );
            (void)addBone( added._name, static_cast<int32>( *parentBone ), added._referencePose ); // 부모가 있는 번호다
        }
        // 합친 뒤의 검증이다 — 부모의 메시 · 클립은 본 번호로 잇는다(`getHierarchyHash`). 차이 칸이 번호를 바꾸는 꼴을 더하면 여기서 멈춘다.
        const uint32 baseBoneCount = base.getBoneCount();
        if ( getHierarchyHash( baseBoneCount ) != base.getHierarchyHash( baseBoneCount ) )
        {
            SW_LOG_ERROR_ID( "Skeleton.VariantMovedBaseBones", "Skeleton variant '%#': the base bones must keep their numbers and parents", sourceLabel );
            bValid = false;
        }
        // 더한 본은 규칙이 없다 — 클립에 없는 본이고 있어도 그대로 재생한다.
        if ( _listClipRule.empty() == false )
            _listClipRule.resize( _listBone.size() );
        for ( const SkeletonSocket& socket : variant._listSocket )
        {
            bValid = addSocket( socket, sourceLabel ) && bValid;
        }
        for ( const SkeletonAttachment& attachment : variant._listAttachment )
        {
            if ( findBoneIndex( attachment._parentBone ).has_value() == false )
            {
                SW_LOG_ERROR_ID( "Skeleton.SkeletonAttachmentNamesUnknown", "Skeleton '%#': attachment '%#' names unknown bone '%#'", sourceLabel, attachment._name.c_str(), attachment._parentBone.c_str() );
                bValid = false;
                continue;
            }
            _listAttachment.push_back( attachment );
        }
        if ( bValid == false )
            clear();
        return bValid;
    }

    bool Skeleton::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        return loadComposed( xml, sourceLabel, ResourceContext::forMerge( &Skeleton::mergeVariantText ) );
    }

    bool Skeleton::loadComposed( string_view xml, string_view sourceLabel, const ResourceContext& context )
    {
        clear();
        string flatText;
        bool   bComposed = false;
        if ( ResourceComposer::compose( xml, sourceLabel, context, flatText, bComposed ) == false )
            return false;
        return parseFlat( bComposed ? string_view( flatText ) : xml, sourceLabel );
    }

    bool Skeleton::mergeVariantText( string_view text, string_view sourceName, IDataInheritanceSource& source, string& outText )
    {
        // 사슬의 순환 · 깊이 · 없는 부모는 공통 `_base` 의 규칙이다. 층의 차이는 덮어쓰기가 아니라 쌓임(뼈 비율 곱 · 클립 규칙)이라 칸으로 합치지 않고 위 층부터 차례로 얹는다.
        vector<DataInheritanceLayer> listLayer;
        Skeleton                     skeleton;
        if ( DataInheritance::collectChain( text, sourceName, source, listLayer ) == false || skeleton.parseFlat( listLayer.front()._text, listLayer.front()._id ) == false )
            return false;
        for ( size_t index = 1; index < listLayer.size(); ++index )
        {
            const DataInheritanceLayer& layer = listLayer[index];
            SkeletonVariant             variant;
            Skeleton                    merged;
            if ( ConfigManager::readConfigXMLDocument( &variant, *SkeletonVariant::StaticType(), layer._text, layer._id.c_str() ) == false || merged.applyVariant( variant, skeleton, layer._id ) == false )
                return false;
            skeleton = std::move( merged );
        }
        SkeletonVariantState state;
        state._listClipRule     = skeleton._listClipRule;
        state._listBasePath     = skeleton._listBasePath;
        state._firstAddedBone   = skeleton._firstAddedBone;
        state._translationRatio = skeleton._translationRatio;
        skeleton._listVariantState.push_back( std::move( state ) );
        outText = skeleton.toXML();
        return true;
    }

    bool Skeleton::parseFlat( string_view xml, string_view sourceLabel )
    {
        // 키는 대소문자까지 맞아야 하고, 모르는 속성 · 자식 · 빠진 칸 · 읽지 못한 값은 오류다(`readConfigXMLDocument` 가 로그로 알린다).
        const string label{ sourceLabel };
        if ( ConfigManager::readConfigXMLDocument( this, *StaticType(), xml, label.c_str() ) && rebuildFromLoaded( sourceLabel ) )
            return true;
        clear();
        return false;
    }

    bool Skeleton::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Animation );
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "Skeleton.SkeletonCouldNotRead", "Skeleton '%#' could not be read", path );
            clear();
            return false;
        }
        return loadComposed( text, path, ResourceContext::forMerge( &Skeleton::mergeVariantText ).withAsset( assetID ) );
    }

    bool Skeleton::rebuildFromLoaded( string_view sourceLabel )
    {
        if ( _listBone.empty() )
        {
            SW_LOG_ERROR_ID( "Skeleton.SkeletonListBoneMustNot", "Skeleton '%#': _listBone must not be empty", sourceLabel );
            return false;
        }
        // 부모가 자식보다 뒤에 있으면 모델 공간을 한 번 훑어서 구할 수 없다. `addBone` 이 거르므로 그 길로 다시 쌓는다.
        const vector<SkeletonBone>         listLoadedBone       = std::move( _listBone );
        const vector<SkeletonAttachment>   listLoadedAttachment = std::move( _listAttachment );
        const vector<SkeletonSocket>       listLoadedSocket     = std::move( _listSocket );
        const vector<SkeletonVariantState> listLoadedState      = std::move( _listVariantState );
        const uint8                        bPartial             = _bPartial;
        clear();
        _bPartial = bPartial;
        for ( const SkeletonBone& bone : listLoadedBone )
        {
            if ( addBone( bone._name, bone._parentIndex, bone._referencePose ) < 0 )
            {
                SW_LOG_ERROR_ID( "Skeleton.SkeletonBoneMustName", "Skeleton '%#': bone '%#' must name an earlier bone as its parent", sourceLabel, bone._name.c_str() );
                return false;
            }
        }
        for ( const SkeletonAttachment& attachment : listLoadedAttachment )
        {
            if ( findBoneIndex( attachment._parentBone ).has_value() == false )
            {
                SW_LOG_ERROR_ID( "Skeleton.SkeletonAttachmentNamesUnknown", "Skeleton '%#': attachment '%#' names unknown bone '%#'", sourceLabel, attachment._name.c_str(), attachment._parentBone.c_str() );
                return false;
            }
            _listAttachment.push_back( attachment );
        }
        for ( const SkeletonSocket& socket : listLoadedSocket )
        {
            if ( addSocket( socket, sourceLabel ) == false )
                return false;
        }
        if ( listLoadedState.empty() )
            return true;
        const SkeletonVariantState& state       = listLoadedState.front();
        const bool                  bStateValid = listLoadedState.size() == 1 && ( state._listClipRule.empty() || state._listClipRule.size() == _listBone.size() );
        if ( bStateValid == false )
        {
            SW_LOG_ERROR_ID( "Skeleton.VariantStateInvalid", "Skeleton '%#': _listVariantState must be one entry with a clip rule per bone", sourceLabel );
            return false;
        }
        _listClipRule     = state._listClipRule;
        _listBasePath     = state._listBasePath;
        _firstAddedBone   = state._firstAddedBone;
        _translationRatio = state._translationRatio;
        return true;
    }

    string Skeleton::toXML() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    bool Skeleton::saveToFile( const AbsolutePath& absPath ) const
    {
        if ( FileUtil::ensureParentDirectoryExists( absPath.str() ) == false )
            return false;
        return ResourceComposer::saveText( absPath.str(), toXML() );
    }
} // namespace sw

namespace sw
{
    SW_DATA_INHERITANCE_MERGE( SkeletonVariant, &Skeleton::mergeVariantText );
} // namespace sw
