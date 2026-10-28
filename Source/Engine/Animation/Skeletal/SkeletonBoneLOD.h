/**
 * @file SkeletonBoneLOD.h
 * @brief 스켈레톤 하나의 본 LOD 테이블(`<이름>.bonelod.xml`)입니다. 화면에서 작아진 캐릭터는 끝 본을 계산하지 않습니다(언리얼 스켈레탈 메시 LOD 의 본 줄이기에 해당합니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

namespace sw
{
    class Skeleton;
    class XMLNode;

    /**
     * @struct SkeletonBoneLODLevel
     * @brief 본 LOD 한 단계입니다. 화면 크기가 `_maxScreenSize` 이하이면 이 단계와 앞 단계들이 뺀 본(과 그 자손)을 풀지 않습니다.
     */
    struct SkeletonBoneLODLevel
    {
        float32               _maxScreenSize{ 0.0f };
        vector<hashed_string> _listRemovedBone;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletonBoneLOD
     * @brief 스켈레톤 옆에 두는 데이터입니다. 임포트가 다시 써도 지워지지 않게 스켈레톤 파일과 따로 둡니다.
     *        임포트한 스켈레톤(`a/knight/knight.skeleton.xml`)이면 옆 폴더 밖의 `a/knight.bonelod.xml` 이고, 옆 폴더는 다시 임포트할 때 통째로 지워집니다. 아니면 같은 폴더의 `<이름>.bonelod.xml` 입니다.
     * @details 형식은 `<SkeletonBoneLOD><levels><level max_screen_size="0.12"><remove><item>kneeIK.l</item>…</remove></level>…</levels></SkeletonBoneLOD>` 이다. 단계는 화면 크기가 작아지는 순서이고(앞 단계보다
     *          `max_screen_size` 가 작아야 한다), 뒤 단계는 앞 단계가 뺀 본을 이어받는다. 화면 크기는 경계 구의 지름이 화면 높이에서 차지하는 비율이다
     *          (`AnimationLODUtil::computeScreenSize`). 빠진 본은 레퍼런스 포즈로 부모를 따라가며, 스키닝에는 그대로 쓰인다.
     *          모르는 키나 스켈레톤에 없는 본 이름은 오류다(`computeMasks`).
     */
    class SW_API SkeletonBoneLOD
    {
    public:
        /** @brief 본 LOD 파일의 확장자입니다. */
        static constexpr string_view kExtension = ".bonelod.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `SkeletonBoneLOD.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "SkeletonBoneLOD", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief XML 을 읽습니다. 형식이 틀리면 오류를 남기고 false 이며 내용은 비웁니다. */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );

        /** @brief 내용 번호입니다. 읽을 때마다 새 번호가 되므로(핫 리로드는 제자리에서 다시 읽습니다) 마스크를 만든 쪽이 변경을 알아챕니다. */
        uint64 getRevision() const { return _revision; }
        /** @brief 단계 수입니다(0 이면 본 LOD 없음). */
        uint32 getLevelCount() const { return static_cast<uint32>( _listLevel.size() ); }
        /** @brief 단계 하나입니다. */
        const SkeletonBoneLODLevel& getLevel( uint32 levelIndex ) const { return _listLevel[levelIndex]; }
        /** @brief @p screenSize 에 맞는 단계입니다. 0 은 모든 본, n 은 `getLevel( n - 1 )` 까지 뺀 것입니다. */
        uint32 selectLevel( float32 screenSize ) const;
        /**
         * @brief 단계마다 본 마스크(본 수, 1 = 계산)를 만듭니다. `outListMask[n]` 은 단계 n + 1 의 마스크입니다(단계 0 은 마스크가 없습니다).
         * @return 스켈레톤에 없는 본 이름이 있으면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] bool computeMasks( const Skeleton& skeleton, vector<vector<uint8>>& outListMask, string_view sourceLabel ) const;

        /**
         * @brief 스켈레톤 경로 옆의 본 LOD 경로를 반환합니다. `a/knight/knight.skeleton.xml`(임포트 옆 폴더, 폴더 이름 = 파일 이름)은 `a/knight.bonelod.xml`,
         *        `a/b.skeleton.xml` 은 `a/b.bonelod.xml` 입니다. 스켈레톤 확장자가 아니면 빈 문자열입니다.
         */
        static string makePathForSkeleton( string_view skeletonPath );
        /** @brief `makePathForSkeleton` 의 역함수입니다. 본 LOD 경로에서 스켈레톤 후보 둘(임포트 옆 폴더 안, 같은 폴더)을 구합니다. */
        static void makeSkeletonCandidatePaths( string_view boneLODPath, string& outImportedPath, string& outSiblingPath );

    private:
        /** @brief 루트 객체를 읽습니다. */
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );

        vector<SkeletonBoneLODLevel> _listLevel;
        uint64                       _revision{ 0 };
    };
} // namespace sw
