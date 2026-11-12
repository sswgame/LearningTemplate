/**
 * @file MatchModeCatalog.h
 * @brief 매칭 모드 정의 데이터입니다. 서버 조립이 기동 때 읽어 매칭 대기열에 넘깁니다.
 * @details 형식은 다음과 같습니다. 속성을 빼면 `MatchModeDef` 의 기본값입니다. 속성은 `MatchModeDef` 스키마로 읽고(`id` 만 손으로 읽는다) 모르는 속성 · 범위 밖 값은 그 모드를 넣지 않는 오류입니다. id 형식은 대기열이 등록할 때 봅니다.
 * @code
 *     <MatchModeCatalog>
 *         <Mode id="duo" teamCount="2" teamSize="2" serverKind="game" regionRelaxMs="30000" maxWaitMs="120000"
 *               baseRatingWindow="100" ratingWindowPerSecond="25" maxRatingWindow="1000" buildVersion="0"/>
 *     </MatchModeCatalog>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /**
     * @class MatchModeCatalog
     * @brief 매칭 모드 정의 모음입니다. 읽은 뒤에는 바꾸지 않습니다.
     */
    class SW_KIT_SERVER_API MatchModeCatalog : public XMLCatalog<MatchModeCatalog>
    {
        friend class XMLCatalog<MatchModeCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<MatchModeCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".matchmodes.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `MatchModeCatalog.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "MatchModeCatalog", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        const vector<MatchModeDef>& getModes() const { return _listMode; }

    private:
        static constexpr const utf8* kXMLRootName = "MatchModeCatalog";
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        /** @brief `<Mode>` 하나를 스키마로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadMode( const XMLNode& node, string_view sourceName );

    private:
        vector<MatchModeDef> _listMode{};
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<MatchModeCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_SERVER_API_EXTERN_TEMPLATE, MatchModeCatalog );
} // namespace sw
