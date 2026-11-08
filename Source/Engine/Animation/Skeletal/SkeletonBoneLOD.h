/**
 * @file SkeletonBoneLOD.h
 * @brief 스켈레톤 하나의 본 LOD 데이터(`<이름>.bonelod.xml`)와 본 LOD 마스크 계산입니다. 화면에서 작아진 캐릭터는 끝 본을 계산하지 않습니다(언리얼 스켈레탈 메시 LOD 의 본 줄이기에 해당합니다).
 * @details 마스크의 출처는 둘이다. 파일이 단계(`<levels>`)를 적으면 그 표를 존중하고, 적지 않으면(또는 파일이 없으면) 그리는 메시의 LOD 단계마다 정점 가중치가 쓰는 본으로 만든다
 *          (`computeMeshMasks` — 언리얼이 LOD 축소 때 가중치 없는 본을 빼는 자리. 기억에 의존). 어느 쪽이든 핵심 본(`computeRequiredMask`)과 그 부모 사슬은 빠지지 않는다.
 *          핵심 본은 파일의 `<coreBones>` 선언(게임플레이 · 카메라 · 루트 모션 · 조준처럼 가중치 · 코드로 알 수 없는 본 — 언리얼 "Bones to Prioritize" · LOD 본 축소 제외 목록과 같은 자리)과,
 *          유닛의 사용처가 내는 목록(소켓 · 어태치먼트 · 리그 · 물리 · 알림 — `SkeletalMeshComponent::requireBones`)의 합이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/IdentityToken.h"
#include "Core/String/hashed_string.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class IDataInheritanceSource;
    class Mesh;
    class Skeleton;
    class XMLNode;
    struct DataInheritanceSchema;

    /**
     * @struct SkeletonBoneLODLevel
     * @brief 본 LOD 한 단계입니다. 화면 크기가 `_maxScreenSize` 이하이면 이 단계와 앞 단계들이 뺀 본(과 그 자손)을 풀지 않습니다.
     */
    REFLECT()
    struct SW_API SkeletonBoneLODLevel
    {
        REFLECT_BODY();

        PROPERTY( Name = "max_screen_size", Required, Min = 0.0 )
        float32               _maxScreenSize{ 0.0f };
        vector<hashed_string> _listRemovedBone; ///< `<remove><item>` 자식(칸이 아니다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletonBoneLOD
     * @brief 스켈레톤 옆에 두는 데이터입니다. 임포트가 다시 써도 지워지지 않게 스켈레톤 파일과 따로 둡니다.
     *        임포트한 스켈레톤(`a/knight/knight.skeleton.xml`)이면 옆 폴더 밖의 `a/knight.bonelod.xml` 이고, 옆 폴더는 다시 임포트할 때 통째로 지워집니다. 아니면 같은 폴더의 `<이름>.bonelod.xml` 입니다.
     * @details 형식은 `<SkeletonBoneLOD _base="…"><coreBones><bone name="camera"/></coreBones><levels><level max_screen_size="0.12"><remove><item>kneeIK.l</item>…</remove></level>…</levels></SkeletonBoneLOD>` 이다.
     *          둘 다 선택이다. `_base` 는 부모 파일(스켈레톤 변형이 부모의 핵심 본을 물려받고 `<bone name="cape" />` 더하기 · `_op="Remove"` 빼기만 적는다 — 공통 상속 `DataInheritance`).
     *          `<levels>` 가 없으면 메시 LOD 에서 자동으로 만든다. 단계는 화면 크기가 작아지는 순서이고(앞 단계보다 `max_screen_size` 가 작아야 한다), 뒤 단계는 앞 단계가 뺀 본을 이어받는다.
     *          화면 크기는 경계 구의 지름이 화면 높이에서 차지하는 비율이다(`LODView::computeScreenSize`). 빠진 본은 레퍼런스 포즈로 부모를 따라가며, 스키닝에는 그대로 쓰인다.
     *          모르는 키, 스켈레톤에 없는 본 이름, 핵심 본(과 그 부모 사슬)을 빼는 단계는 오류다(`computeMasks` · `computeRequiredMask`).
     */
    class SW_API SkeletonBoneLOD
    {
        friend struct AssetTypeLoader;

    public:
        /** @brief 본 LOD 파일의 확장자입니다. */
        static constexpr string_view kExtension = ".bonelod.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `SkeletonBoneLOD.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "SkeletonBoneLOD", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief XML 을 읽습니다(`_base` 는 리소스에서 부모를 읽어 펼친다). 형식이 틀리면 오류를 남기고 false 이며 내용은 비웁니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /** @brief XML 을 읽습니다. `_base` 의 부모는 @p source 에서 읽습니다(테스트 · 쿠커). */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel, IDataInheritanceSource& source );

        /** @brief 내용 토큰입니다(`IdentityToken`). 읽을 때마다 새 토큰이 되므로(핫 리로드는 제자리에서 다시 읽습니다) 마스크를 만든 쪽이 토큰을 들고 비교해 변경을 알아챕니다. */
        const IdentityToken& getRevision() const { return _revision; }
        /** @brief 손으로 쓴 단계 수입니다(0 이면 메시 LOD 에서 자동으로 만든다). */
        uint32 getLevelCount() const { return static_cast<uint32>( _listLevel.size() ); }
        /** @brief 단계 하나입니다. */
        const SkeletonBoneLODLevel& getLevel( uint32 levelIndex ) const { return _listLevel[levelIndex]; }
        /** @brief 데이터가 선언한 핵심 본입니다(부모 파일에서 물려받은 것 포함). */
        const vector<hashed_string>& getCoreBones() const { return _listCoreBone; }
        /** @brief @p screenSize 에 맞는 단계입니다. 0 은 모든 본, n 은 `getLevel( n - 1 )` 까지 뺀 것입니다. */
        uint32 selectLevel( float32 screenSize ) const;
        /**
         * @brief 손으로 쓴 단계마다 본 마스크(본 수, 1 = 계산)를 만듭니다. `outListMask[n]` 은 단계 n + 1 의 마스크입니다(단계 0 은 마스크가 없습니다).
         * @param requiredMask `computeRequiredMask` 의 결과입니다. 단계가 이 본을 빼면(자손으로 빠지는 것 포함) 오류입니다.
         * @return 스켈레톤에 없는 본 이름이 있거나 핵심 본을 빼면 그 이름과 함께 오류를 남기고 false(마스크 없음)를 반환합니다.
         */
        [[nodiscard]] bool computeMasks( const Skeleton& skeleton, const vector<uint8>& requiredMask, vector<vector<uint8>>& outListMask, string_view sourceLabel ) const;

        /**
         * @brief 빼면 안 되는 본의 마스크(본 수, 1 = 핵심)를 만듭니다. 스켈레톤의 소켓 · 어태치먼트 부모 본, @p pBoneLOD 가 선언한 핵심 본, 사용처의 본 @p listUserBone 과 그 부모 사슬입니다.
         * @details 사용처의 본 이름이 스켈레톤에 없으면 건너뛴다(그 사용처가 자기 오류를 낸다). 선언한 핵심 본이 없으면 데이터 오류다.
         * @return 선언한 핵심 본이 스켈레톤에 없으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool computeRequiredMask( const Skeleton& skeleton, const SkeletonBoneLOD* pBoneLOD, const vector<hashed_string>& listUserBone, vector<uint8>& outRequiredMask,
                                                       string_view sourceLabel );
        /**
         * @brief 메시 LOD 마다 그 단계 정점이 가중치로 쓰는 본과 @p requiredMask 를 남긴 마스크를 만듭니다(쓰이는 본의 부모 사슬도 남는다). `outListMask[n]` 은 메시 LOD n + 1 의 것입니다.
         * @details 조립 캐릭터는 합친 메시(부품 합집합)가 들어온다 — 부품 하나로 만들면 다른 부품이 쓰는 본이 빠진다. 메시 LOD 가 없으면 비웁니다.
         */
        static void computeMeshMasks( const Skeleton& skeleton, const Mesh& mesh, const vector<uint8>& requiredMask, vector<vector<uint8>>& outListMask );

        /** @brief 상속 규칙입니다(`coreBones` 는 `name` 열쇠 목록, `levels` 는 통째로 바뀐다). 팩 쿠커가 루트 이름으로 찾는다. */
        static DataInheritanceSchema makeInheritanceSchema();

        /**
         * @brief 스켈레톤 경로 옆의 본 LOD 경로를 반환합니다. `a/knight/knight.skeleton.xml`(임포트 옆 폴더, 폴더 이름 = 파일 이름)은 `a/knight.bonelod.xml`,
         *        `a/b.skeleton.xml` 은 `a/b.bonelod.xml` 입니다. 스켈레톤 확장자가 아니면 빈 문자열입니다.
         */
        static string makePathForSkeleton( string_view skeletonPath );
        /** @brief `makePathForSkeleton` 의 역함수입니다. 본 LOD 경로에서 스켈레톤 후보 둘(임포트 옆 폴더 안, 같은 폴더)을 구합니다. */
        static void makeSkeletonCandidatePaths( string_view boneLODPath, string& outImportedPath, string& outSiblingPath );

    private:
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<SkeletonBoneLOD>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 루트 객체를 읽습니다. */
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );
        /** @brief 펼친 글을 읽습니다. */
        [[nodiscard]] bool parseFlat( string_view xml, string_view sourceLabel );

    private:
        vector<SkeletonBoneLODLevel> _listLevel;
        vector<hashed_string>        _listCoreBone;
        IdentityToken                _revision;
    };
} // namespace sw
