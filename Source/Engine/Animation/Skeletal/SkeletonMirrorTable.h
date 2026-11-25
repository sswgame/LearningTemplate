/**
 * @file SkeletonMirrorTable.h
 * @brief 스켈레톤 하나의 좌우 본 짝 표(`<이름>.mirrortable.xml`)입니다. 포즈 노드 `Mirror`(`MirrorPoseNode`)가 이 표로 왼쪽 본의 포즈를 오른쪽 본에 옮깁니다.
 * @details 언리얼 `UMirrorDataTable`(본 이름 → 짝 이름 행) 자리다. 스켈레톤 파일은 임포트가 다시 쓰므로 손으로 적는 짝은 본 LOD 처럼 옆 파일에 둔다(`Skeleton::makeSidePath`).
 *          형식: `<SkeletonMirrorTable><_listPair><SkeletonMirrorPair _bone="hand_l" _mirroredBone="hand_r" /></_listPair></SkeletonMirrorTable>`.
 *          짝은 한 줄로 양쪽이다(hand_l ↔ hand_r). 표에 없는 본은 자기 자신과 짝이다(척추 · 머리처럼 대칭면 위의 본).
 *          대칭 축은 이 표가 아니라 노드 설정(`<pose_node type="Mirror" mirror_axis="X">`)이다 — 같은 스켈레톤을 다른 축으로 뒤집는 그래프도 표 하나를 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/IdentityToken.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Skeleton;

    /** @brief 좌우 짝 한 줄입니다. 둘 다 스켈레톤에 있어야 하고 서로 달라야 합니다. */
    REFLECT()
    struct SW_API SkeletonMirrorPair
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Bone on one side" )
        hashed_string _bone;
        PROPERTY( Required, Tooltip = "Bone on the other side that takes this bone's mirrored pose (and gives it its own)" )
        hashed_string _mirroredBone;
    };

    /**
     * @class SkeletonMirrorTable
     * @brief 좌우 본 짝 표입니다. 파일 머리말 참고.
     */
    REFLECT()
    class SW_API SkeletonMirrorTable
    {
        friend struct AssetTypeLoader;

    public:
        REFLECT_BODY();

        /** @brief 미러 표 파일의 확장자입니다. */
        static constexpr string_view kExtension = ".mirrortable.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 SkeletonMirrorTable.cpp 끝의 공유 캐시 줄 — 스켈레톤 · 본 LOD 와 같은 자리). */
        static constexpr AssetTypeDef kAssetType{ "SkeletonMirrorTable", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief XML 본문을 읽습니다. 모르는 키 · 빠진 칸이면 오류를 남기고 false 이며 표는 빕니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /** @brief 짝 줄입니다. */
        const vector<SkeletonMirrorPair>& getPairs() const { return _listPair; }
        /** @brief 내용 토큰입니다. 읽을 때마다 새 토큰이라(공유 캐시는 다시 읽으면 새 객체를 준다) 짝 번호 표를 만든 쪽이 들고 비교해 다시 읽힌 것을 알아챕니다. */
        const IdentityToken& getRevision() const { return _revision; }

        /**
         * @brief 본마다 짝 본 번호(`outListMirroredBone[i]`, 짝이 없으면 i)를 만듭니다. `MirrorPoseNode` 가 평가에 받는 표입니다.
         * @return 스켈레톤에 없는 본 · 자기 자신과 짝 · 두 짝에 든 본이 있으면 오류를 남기고 false 입니다(표는 빕니다).
         */
        [[nodiscard]] bool buildBoneMap( const Skeleton& skeleton, vector<uint32>& outListMirroredBone, string_view sourceLabel ) const;

        /** @brief 스켈레톤 경로 옆의 미러 표 경로입니다(`Skeleton::makeSidePath`). 스켈레톤 확장자가 아니면 빈 문자열입니다. */
        static string makePathForSkeleton( string_view skeletonPath );

    private:
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<SkeletonMirrorTable>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );

    private:
        PROPERTY( Required, Tooltip = "Left-right bone pairs; bones not listed mirror onto themselves" )
        vector<SkeletonMirrorPair> _listPair;
        IdentityToken              _revision; ///< 읽을 때마다 새로 받는다(`getRevision`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletonMirrorBoneMap
     * @brief 스켈레톤 하나와 그 미러 표로 만든 본마다 짝 본 번호(`SkeletonMirrorTable::buildBoneMap`)를 들고, 표가 다시 읽히거나 스켈레톤이 바뀌면 다시 만듭니다.
     * @details 스켈레톤 곁 파일을 고르는 쪽(`SkeletalMeshComponent` — 본 LOD 와 같은 규칙)이 하나 들고, 포즈 노드 평가가 `find` 로 받는다(`AnimationPoseParams::_pMirroredBone`).
     *          짝 표가 틀리면 같은 내용으로 다시 만들지 않는다 — 오류는 표가 바뀔 때마다 한 번이다.
     */
    class SW_API SkeletonMirrorBoneMap
    {
    public:
        SkeletonMirrorBoneMap();

        /** @brief 쓰는 표를 바꿉니다(nullptr 이면 표 없음). 다음 `find` 가 다시 만듭니다. */
        void setTable( shared_ptr<const SkeletonMirrorTable> table );
        /** @brief 쓰는 표입니다(없으면 nullptr). */
        const SkeletonMirrorTable* findTable() const { return _table.get(); }
        /** @brief @p skeleton 의 짝 번호 표입니다. 표가 없거나 스켈레톤과 맞지 않으면(오류는 만들 때 한 번) nullptr 입니다. */
        const vector<uint32>* find( const Skeleton& skeleton, string_view sourceLabel );

    private:
        SharedAssetRef<SkeletonMirrorTable> _table;
        vector<uint32>                      _listMirroredBone;
        IdentityToken                       _builtRevision;  ///< 짝 번호 표를 만든 표 내용 토큰이다
        const Skeleton*                     _pBuiltSkeleton; ///< 짝 번호 표를 만든 스켈레톤이다(동일성만 확인한다)
    };

    /** @brief 스켈레톤 옆의 미러 표를 나눠 줍니다("SkeletonMirrorTable"). 짝 번호 표를 든 쪽은 `SkeletonMirrorTable::getRevision` 으로 다시 읽힌 것을 알아챕니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_API_EXTERN_TEMPLATE, SkeletonMirrorTable );
} // namespace sw
