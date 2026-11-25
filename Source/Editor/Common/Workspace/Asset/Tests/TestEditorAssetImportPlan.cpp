// EditorAssetImportPlanTest: 임포트 잡이 바뀐 원본을 임포터마다 한 묶음으로 나눈다(스탬프와 원본 사이 순서 의존이 폴더를 넘으므로 폴더로 쪼개지 않는다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Workspace/Asset/EditorAssetImportJob.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorAssetImportPlanTestInternal
    {
        static void importA( AssetSourceBatch& /*batch*/ ) {}
        // 본문이 importA 와 달라야 한다 — 같으면 Release 링커가 두 함수를 하나로 접어(ICF) 주소가 같아지고, 계획이 "다른 임포터" 를 가르지 못한다.
        static void importB( AssetSourceBatch& batch ) { batch._listItem.clear(); }
    };
} // namespace

/**
 * @brief [EditorAssetImportPlanTest] 같은 임포터의 원본은 폴더가 달라도 한 묶음이고 입력 순서를 지키며, 다른 임포터는 같은 폴더라도 따로다
 */
SW_TEST_CASE( EditorAssetImportPlanTest, OneBatchPerImporter )
{
    using Internal = EditorAssetImportPlanTestInternal;
    const vector<EditorAssetImportItem> listItem{
        EditorAssetImportItem{  "raw/a/x.glb", &Internal::importA},
        EditorAssetImportItem{  "raw/a/t.png", &Internal::importB},
        EditorAssetImportItem{  "raw/b/y.glb", &Internal::importA},
        EditorAssetImportItem{"raw/a/x/z.glb", &Internal::importA},
    };
    const vector<vector<uint32>> listBatch = EditorAssetImportJob::planBatches( listItem );
    SW_REQUIRE_TRUE( listBatch.size() == 2 );
    SW_EXPECT_TRUE( ( listBatch[0] == vector<uint32>{ 0, 2, 3 } ) );
    SW_EXPECT_TRUE( ( listBatch[1] == vector<uint32>{ 1 } ) );
    SW_EXPECT_TRUE( EditorAssetImportJob::planBatches( {} ).empty() );
}
