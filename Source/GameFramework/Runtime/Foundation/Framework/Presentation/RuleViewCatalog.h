/**
 * @file RuleViewCatalog.h
 * @brief 규칙 id → 뷰(메시 에셋 · 모델 폭) 카탈로그입니다. 시뮬레이션 규칙(유닛 · 건물 id)이 어떤 모델로 보이는지를 데이터로 둡니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /**
     * @brief 규칙 id 하나의 뷰입니다(`<View id mesh width/>` 한 줄). 세 칸 모두 필수다.
     * @details 메시는 타입 있는 에셋 참조라 쿠킹 도달 계산이 데이터에서 바로 본다 — 예전에는 게임 코드가 이름을 경로로 조립해 쿠커가 놓칠 수 있었다(감사 K-7).
     *          언리얼 Mass Representation 의 "엔티티 → 시각 설명" 표, 유니티 Addressables 키 표와 같은 자리다.
     */
    REFLECT()
    struct SW_GF_API RuleViewDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{}; ///< 규칙(시뮬레이션 카탈로그)의 id
        PROPERTY( Required )
        AssetRef<MeshAssetFormat> _mesh{}; ///< 그릴 메시 에셋(`.mesh`)
        PROPERTY( Required, Min = 0.01, Max = 1000.0 )
        float32 _width{}; ///< 모델의 가로 폭(가장 넓은 수평 축)이다. 뷰가 칸 · 지름에 맞춰 늘릴 때 나눈다
    };
} // namespace sw

namespace sw
{
    /**
     * @class RuleViewCatalog
     * @brief `<RuleViewCatalog><View id="marine" mesh="game/starskirmish/models/craft_speeder_a.mesh" width="2.1"/></RuleViewCatalog>` 를 읽습니다(`*.views.xml`).
     * @details 줄이 없는 규칙 id 를 어떻게 그릴지(그리지 않음 · 색 상자)는 쓰는 게임이 정한다. 모든 규칙이 줄을 가져야 하는 게임은 시작할 때 맞춰 본다.
     */
    class SW_GF_API RuleViewCatalog : public GameCatalog<RuleViewDef, XMLCatalog<RuleViewCatalog>>
    {
        friend class XMLCatalog<RuleViewCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<RuleViewCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".views.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `RuleViewCatalog.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "RuleViewCatalog", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 모든 뷰의 메시 경로입니다(겹치는 메시는 한 번). 판을 열 때 미리 읽는다. */
        vector<string> makeAllMeshPaths() const;

    private:
        static constexpr const utf8* kXMLRootName = "RuleViewCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        /** @brief `<View>` 한 줄을 스키마로 읽습니다. 칸이 빠졌거나 범위 밖이면 넣지 않고 false 입니다. */
        [[nodiscard]] bool loadView( const XMLNode& node, string_view sourceName );
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<RuleViewCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, RuleViewCatalog );
} // namespace sw
