// MaterialLibraryTest — 외형 머티리얼 라이브러리(Content/engine/materials/library)의 모든 프리셋이 읽히고, 갈래 규칙(정렬 · 블렌드 · 엽 조합)을 지키는지(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialTypes.h"

using namespace sw;

namespace
{
    struct MaterialLibraryTestUtil
    {
        static constexpr const utf8* kLibraryFolder = "Content/engine/materials/library";
        static constexpr const utf8* kRootID        = "engine/materials/library/appearance.material";

        struct LoadedMaterial
        {
            string               _id;
            shared_ptr<Material> _material;
        };

        [[nodiscard]] static bool hasDefine( const Material& material, string_view define )
        {
            return VectorUtil::contains( material.getPermutations()._listAlwaysDefine, string( define ) );
        }

        /**
         * @brief 엽이 겹쳐 G버퍼 칸(셰이딩 모델 번호 하나 + 사용자 칸 넷)에 들지 않는 갈래인지 — appearance_lit.hlsl 의 G버퍼 #error 와 같은 조건이다.
         * @details 박막(F0 색) · 털(가닥 방향 + 흡수 색 + 기울기) · 그물 아래층(BRDF 둘) · 시엔 + 비등방/산란(엽 둘).
         */
        [[nodiscard]] static bool needsForwardShading( const Material& material )
        {
            const bool bSheen = hasDefine( material, "MATERIAL_LOBE_SHEEN" );
            return hasDefine( material, "MATERIAL_LOBE_THIN_FILM" ) || hasDefine( material, "MATERIAL_LOBE_STRAND" ) || hasDefine( material, "MATERIAL_NET_UNDERLAYER" ) ||
                   ( bSheen && ( hasDefine( material, "MATERIAL_LOBE_ANISOTROPY" ) || hasDefine( material, "MATERIAL_LOBE_SUBSURFACE" ) ) );
        }

        /** @brief 라이브러리의 .material 을 모두 리소스 id 로 읽습니다(`_base` 를 펼친 결과). 하나라도 못 읽으면 실패를 남긴다. */
        [[nodiscard]] static vector<LoadedMaterial> loadAll()
        {
            vector<LoadedMaterial> listLoaded;
            vector<string>         listPath;
            const string           folder = ProcessRoots::get()._project.str() + "/" + kLibraryFolder;
            SW_EXPECT_TRUE( FileUtil::collectFiles( folder, ".material", listPath, true ) );
            for ( const string& path : listPath )
            {
                const size_t contentAt = path.find( "Content/" );
                SW_EXPECT_TRUE( contentAt != string::npos );
                if ( contentAt == string::npos )
                    continue;
                LoadedMaterial loaded{ path.substr( contentAt + 8 ), Material::create() };
                SW_EXPECT_TRUE_MSG( loaded._material->loadFromAsset( AssetID{ loaded._id } ), loaded._id.c_str() );
                listLoaded.push_back( std::move( loaded ) );
            }
            return listLoaded;
        }
    };
} // namespace

/** @brief [MaterialLibraryTest] 모든 프리셋이 읽히고 뿌리와 같은 셰이더 · 같은 프로퍼티 순서다(셰이더 구조체와 1:1 — 프리셋이 칸을 더하거나 빼지 않는다) */
SW_TEST_CASE( MaterialLibraryTest, EveryPresetLoadsWithTheRootLayout )
{
    const vector<MaterialLibraryTestUtil::LoadedMaterial> listLoaded = MaterialLibraryTestUtil::loadAll();
    SW_REQUIRE_TRUE( listLoaded.size() > 40 );
    const shared_ptr<Material> root = Material::create();
    SW_REQUIRE_TRUE( root->loadFromAsset( AssetID{ MaterialLibraryTestUtil::kRootID } ) );
    for ( const MaterialLibraryTestUtil::LoadedMaterial& loaded : listLoaded )
    {
        const Material& material = *loaded._material;
        SW_EXPECT_TRUE_MSG( material.getShaderPath() == root->getShaderPath(), loaded._id.c_str() );
        // 디퍼드는 엽 하나를 셰이딩 모델 번호로 싣는다 — 겹치는 갈래만 포워드 전용이고, 나머지가 포워드 전용이면 디퍼드에서 쓸데없이 포워드로 그린다.
        SW_EXPECT_TRUE_MSG( MaterialLibraryTestUtil::needsForwardShading( material ) == MaterialLibraryTestUtil::hasDefine( material, "MATERIAL_FORWARD_ONLY" ), loaded._id.c_str() );
        SW_EXPECT_EQUAL( root->getProperties().size(), material.getProperties().size() );
        for ( size_t index = 0; index < root->getProperties().size() && index < material.getProperties().size(); ++index )
            SW_EXPECT_TRUE_MSG( root->getProperties()[index]._name == material.getProperties()[index]._name, loaded._id.c_str() );
    }
}

/**
 * @brief [MaterialLibraryTest] 정렬 · 블렌드 규칙 — 굴절(장면 사본을 읽는다)은 반투명 목록에서 얇은 반투명보다 먼저 그린다
 * @details 사본은 반투명 전체 앞에 떠 둔 것이라 굴절 면보다 먼저 그려진 반투명은 그 픽셀에서 지워진다(scene_refraction.hlsli). 그래서 굴절의 renderQueue 가 더 작아야 한다.
 */
SW_TEST_CASE( MaterialLibraryTest, RefractionDrawsBeforeOtherTranslucency )
{
    int32 maxRefractionQueue = std::numeric_limits<int32>::min();
    int32 minOtherQueue      = std::numeric_limits<int32>::max();
    for ( const MaterialLibraryTestUtil::LoadedMaterial& loaded : MaterialLibraryTestUtil::loadAll() )
    {
        const Material& material     = *loaded._material;
        const bool      bRefraction  = MaterialLibraryTestUtil::hasDefine( material, "MATERIAL_REFRACTION" );
        const bool      bTranslucent = MaterialLibraryTestUtil::hasDefine( material, "MATERIAL_BLEND_TRANSLUCENT" );
        // 블렌드 define 과 반투명 목록은 함께 움직인다 — 하나만 있으면 알파가 새거나 불투명 목록에서 섞인다.
        SW_EXPECT_TRUE_MSG( bTranslucent == ( material.getAlphaMode() == MaterialAlphaMode::Blend ), loaded._id.c_str() );
        if ( bRefraction )
            maxRefractionQueue = std::max( maxRefractionQueue, material.getRenderQueue() );
        else if ( bTranslucent )
            minOtherQueue = std::min( minOtherQueue, material.getRenderQueue() );
        SW_EXPECT_TRUE_MSG( bRefraction == false || bTranslucent, loaded._id.c_str() );
    }
    SW_REQUIRE_TRUE( maxRefractionQueue != std::numeric_limits<int32>::min() && minOtherQueue != std::numeric_limits<int32>::max() );
    SW_EXPECT_TRUE( maxRefractionQueue < minOtherQueue );
}

/** @brief [MaterialLibraryTest] 엽 · 기능 조합 — 털 껍질은 정점 이동 + 알파 컷, 오려 내는 그물은 알파 컷 · 양면, 피부 위 그물은 불투명 */
SW_TEST_CASE( MaterialLibraryTest, FeatureCombinationsMatchTheirRenderState )
{
    for ( const MaterialLibraryTestUtil::LoadedMaterial& loaded : MaterialLibraryTestUtil::loadAll() )
    {
        const Material& material = *loaded._material;
        if ( MaterialLibraryTestUtil::hasDefine( material, "MATERIAL_FUR_SHELL" ) )
        {
            SW_EXPECT_TRUE_MSG( MaterialLibraryTestUtil::hasDefine( material, "MATERIAL_VERTEX_DEFORM" ), loaded._id.c_str() );
            SW_EXPECT_TRUE_MSG( material.getAlphaMode() == MaterialAlphaMode::Mask, loaded._id.c_str() );
        }
        if ( MaterialLibraryTestUtil::hasDefine( material, "MATERIAL_PATTERN_NET" ) )
        {
            const bool bUnderlayer = MaterialLibraryTestUtil::hasDefine( material, "MATERIAL_NET_UNDERLAYER" );
            SW_EXPECT_TRUE_MSG( ( material.getAlphaMode() == MaterialAlphaMode::Mask ) != bUnderlayer, loaded._id.c_str() );
        }
        // 무늬 define 은 하나만 — 둘이면 셰이더의 #elif 가 첫째만 고른다.
        int32 patternCount = 0;
        for ( const string& define : material.getPermutations()._listAlwaysDefine )
            patternCount += StringUtil::startsWith( define, "MATERIAL_PATTERN_" ) ? 1 : 0;
        SW_EXPECT_TRUE_MSG( patternCount <= 1, loaded._id.c_str() );
    }
}
