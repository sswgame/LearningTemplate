#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorAssetTypeActions.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 오디오 종류의 가짜 동작 — 열기만 처리한다. EditorTest 에는 실제 종류 동작(ImGui 를 그린다)이 링크되지 않는다. */
    class FakeAudioAssetTypeActions final : public IEditorAssetTypeActions
    {
    public:
        virtual EditorAssetKind getKind() const override { return EditorAssetKind::Audio; }
        virtual bool            open( string_view /*relativePath*/ ) const override { return true; }
    };
} // namespace

/**
 * @brief [EditorAssetTypeActionsTest] 등록자는 종류로 등록하고, 경로는 그 종류의 동작을 찾는다
 * @details 썸네일 · 열기 · 뷰포트 드롭이 콘텐츠 브라우저와 `EditorAssetCommands` 에서 종류별 if-체인이었다(그중 머티리얼 열기 분기는 패널이 먼저
 *          받아 닿지 않는 죽은 코드였다). 이제 종류의 코드 파일이 등록하고 부르는 쪽은 `findActionsForPath` 하나로 찾는다.
 */
SW_TEST_CASE( EditorAssetTypeActionsTest, RegistrarRegistersByKindAndPathFindsIt )
{
    SW_ASSERT_TRUE( EditorAssetTypeActionsRegistry::findActions( EditorAssetKind::Audio ) == nullptr );
    {
        const EditorAssetTypeActionsRegistrar<FakeAudioAssetTypeActions> registrar{};
        const IEditorAssetTypeActions* const                             pActions = &registrar.getActions();
        SW_EXPECT_TRUE( EditorAssetTypeActionsRegistry::findActions( EditorAssetKind::Audio ) == pActions );
        SW_EXPECT_TRUE( EditorAssetTypeActionsRegistry::findActionsForPath( "game/empty/audio/hit.wav" ) == pActions );
        SW_EXPECT_TRUE( EditorAssetTypeActionsRegistry::findActionsForPath( "notes/todo.txt" ) == nullptr );
        SW_EXPECT_TRUE( EditorAssetTypeActionsRegistry::findActions( EditorAssetKind::Unknown ) == nullptr );

        // 재정의하지 않은 동작은 "처리하지 않음" 이다 — 부르는 쪽이 일반 동작으로 넘어간다.
        SW_EXPECT_TRUE( pActions->open( "game/empty/audio/hit.wav" ) );
        SW_EXPECT_FALSE( pActions->drawThumbnail( nullptr, float2{ 0.0f, 0.0f }, float2{ 64.0f, 64.0f } ) );
        SW_EXPECT_FALSE( pActions->dropInViewport( nullptr, "game/empty/audio/hit.wav", float3{ 0.0f, 0.0f, 0.0f } ) );

        // 같은 종류의 둘째 등록은 거절되고, 그것이 내려가도 첫째는 남는다.
        {
            test::ScopedDefensiveTestLog                                     expected( "a second registration for one asset kind" );
            const EditorAssetTypeActionsRegistrar<FakeAudioAssetTypeActions> second{};
            SW_EXPECT_TRUE( EditorAssetTypeActionsRegistry::findActions( EditorAssetKind::Audio ) == pActions );
        }
        SW_EXPECT_TRUE( EditorAssetTypeActionsRegistry::findActions( EditorAssetKind::Audio ) == pActions );
    }
    SW_EXPECT_TRUE( EditorAssetTypeActionsRegistry::findActions( EditorAssetKind::Audio ) == nullptr );
}
