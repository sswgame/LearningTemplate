/**
 * @file VertexAnimationCookList.h
 * @brief 정점 애니메이션(VAT) 쿠킹 목록(`*.vertexanimation.xml`) — 쿠커(DevTools `VertexAnimationCooker`)가 이 (메시, 클립)을 `.vat` 로 베이크합니다.
 * @details 배포본은 쿠킹본을 팩에서 읽고(`AnimationCrowd::findVertexAnimationMesh`), Dev 는 쿠킹 폴더를 마운트하지 않아 처음 쓸 때 같은 함수
 *          (`MeshVertexAnimationBaker`)로 베이크합니다 — 두 길의 결과가 바이트까지 같습니다. 쿠킹은 `App --cook-scenes` 단계가 부릅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Animation/AnimationSet.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class XMLNode;

    /**
     * @struct VertexAnimationCookList
     * @brief 쿠킹 목록 하나(`<이름>.vertexanimation.xml`): `<VertexAnimationCookList mesh skeleton animation_set><clips><item>클립 이름</item>…</clips></VertexAnimationCookList>`.
     * @details 모르는 속성 · 자식 · 없는 메시 · 스켈레톤 · 클립 파일은 오류입니다(`ResourceDataSchemaTest` 가 확인합니다). 루트 모션은 묶지 않고 베이크합니다
     *          (루트 모션을 뽑는 유닛은 쿠킹본을 쓰지 않고 처음 쓸 때 베이크한다).
     */
    REFLECT()
    struct SW_API VertexAnimationCookList
    {
        REFLECT_BODY();

        /** @brief 목록 파일의 확장자입니다. */
        static constexpr string_view kExtension = ".vertexanimation.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `VertexAnimationCookList.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "VertexAnimationCookList", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 굽는 메시입니다 — 저장 꼴은 GUID 참조(`AssetRefBase`). 경로 글 칸이었을 때는 다른 칸의 `AssetRef<MeshAssetFormat>` `mesh` 와 이름이 같아 GUID 이전 도구가 칸 꼴을 가르지 못했다. */
        PROPERTY( Name = "mesh", Required )
        AssetRef<MeshAssetFormat> _meshPath;
        PROPERTY( Name = "skeleton", Required )
        AssetRef<Skeleton> _skeletonPath;
        PROPERTY( Name = "animation_set", Required )
        AssetRef<AnimationSet> _animationSetPath;
        AnimationSet           _animationSet; ///< 읽기가 채운다(칸이 아니다)
        vector<hashed_string>  _listClip;     ///< `<clips><item>` 자식(칸이 아니다)

        /** @brief XML 을 읽습니다(가리키는 파일이 있는지도 봅니다). 틀리면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 클립 이름의 파일 경로입니다(셋의 표 — 애니메이터와 같은 규칙). 셋에 없으면 빈 글입니다. */
        string makeClipPath( const hashed_string& clipName ) const;

    private:
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );
    };
} // namespace sw
