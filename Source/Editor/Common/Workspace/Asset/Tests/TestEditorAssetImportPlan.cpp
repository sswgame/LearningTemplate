// EditorAssetImportPlanTest — 임포트 잡이 원본을 동시에 돌 묶음으로 나눈다: 병렬 임포터의 다른 폴더는 따로, 같은 폴더 · 조상 폴더는 한 묶음, 병렬 능력이 없는 임포터는 차례 목록.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Asset/EditorAssetImportJob.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorAssetImportPlanTestInternal
    {
        static bool importA( const AssetPaths& /*assetPaths*/, string_view /*relativePath*/ ) { return true; }
        // 본문이 importA 와 달라야 한다 — 같으면 Release 링커가 두 함수를 하나로 접어(ICF) 주소가 같아지고, 계획이 "다른 임포터" 를 가르지 못한다.
        static bool importB( const AssetPaths& /*assetPaths*/, string_view relativePath ) { return relativePath.empty() == false; }

        static EditorAssetImportItem make( const utf8* pPath, bool ( *pfnImport )( const AssetPaths&, string_view ), AssetImportThread thread )
        {
            return EditorAssetImportItem{ pPath, pfnImport, thread };
        }
    };
} // namespace

/**
 * @brief [EditorAssetImportPlanTest] 다른 폴더는 따로 돌고, 같은 폴더와 그 아래 폴더(옆 폴더 `x/` 아래 원본)는 한 묶음이며 입력 순서를 지킨다
 */
SW_TEST_CASE( EditorAssetImportPlanTest, FoldersSplitAndAncestorsJoin )
{
    using Internal                                = EditorAssetImportPlanTestInternal;
    const AssetImportThread             kParallel = AssetImportThread::WorkerParallel;
    const vector<EditorAssetImportItem> listItem{
        Internal::make( "raw/a/x.glb", &Internal::importA, kParallel ),                 // 0
        Internal::make( "raw/b/y.glb", &Internal::importA, kParallel ),                 // 1
        Internal::make( "raw/a/x/z.glb", &Internal::importA, kParallel ),               // 2 — 0 의 옆 폴더 아래
        Internal::make( "raw/A/w.glb", &Internal::importA, kParallel ),                 // 3 — 대소문자만 다른 0 의 폴더
        Internal::make( "raw/b/t.png", &Internal::importB, kParallel ),                 // 4 — 같은 폴더라도 다른 임포터는 따로
        Internal::make( "raw/c/s.glb", &Internal::importA, AssetImportThread::Worker ), // 5 — 병렬 능력 없음
    };
    EditorAssetImportPlan plan;
    EditorAssetImportJob::planGroups( listItem, plan );

    SW_REQUIRE_TRUE( plan._listParallelGroup.size() == 3 );
    SW_EXPECT_TRUE( ( plan._listParallelGroup[0] == vector<uint32>{ 0, 2, 3 } ) );
    SW_EXPECT_TRUE( ( plan._listParallelGroup[1] == vector<uint32>{ 1 } ) );
    SW_EXPECT_TRUE( ( plan._listParallelGroup[2] == vector<uint32>{ 4 } ) );
    SW_EXPECT_TRUE( ( plan._listSerial == vector<uint32>{ 5 } ) );
}

/**
 * @brief [EditorAssetImportPlanTest] 뒤에 온 조상 폴더 원본이 먼저 생긴 두 자손 묶음을 하나로 잇는다
 */
SW_TEST_CASE( EditorAssetImportPlanTest, AncestorMergesEarlierGroups )
{
    using Internal                                = EditorAssetImportPlanTestInternal;
    const AssetImportThread             kParallel = AssetImportThread::WorkerParallel;
    const vector<EditorAssetImportItem> listItem{
        Internal::make( "raw/m/p/a.glb", &Internal::importA, kParallel ),
        Internal::make( "raw/m/q/b.glb", &Internal::importA, kParallel ),
        Internal::make( "raw/m/c.glb", &Internal::importA, kParallel ),
    };
    EditorAssetImportPlan plan;
    EditorAssetImportJob::planGroups( listItem, plan );
    SW_REQUIRE_TRUE( plan._listParallelGroup.size() == 1 );
    SW_EXPECT_TRUE( ( plan._listParallelGroup[0] == vector<uint32>{ 0, 1, 2 } ) );
    SW_EXPECT_TRUE( plan._listSerial.empty() );
}
