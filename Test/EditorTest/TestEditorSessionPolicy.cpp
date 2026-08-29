#include "pch.h"

#include "Editor/Common/EditorSessionPolicy.h"

#include "TestFramework/TestFramework.h"

SW_TEST_CASE( EditorSessionPolicyTest, UnsavedPromptFollowsDirtyFlag )
{
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::needsUnsavedPrompt( false ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::needsUnsavedPrompt( true ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, ChoiceRouting )
{
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldSaveBeforeAction( sw::editor::EditorUnsavedChoice::Save ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldSaveBeforeAction( sw::editor::EditorUnsavedChoice::Discard ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldSaveBeforeAction( sw::editor::EditorUnsavedChoice::Cancel ) );

	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldProceedWithAction( sw::editor::EditorUnsavedChoice::Save ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldProceedWithAction( sw::editor::EditorUnsavedChoice::Discard ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldProceedWithAction( sw::editor::EditorUnsavedChoice::Cancel ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldProceedWithAction( sw::editor::EditorUnsavedChoice::None ) );

	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldClearDirtyWithoutSave( sw::editor::EditorUnsavedChoice::Discard ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldClearDirtyWithoutSave( sw::editor::EditorUnsavedChoice::Save ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, SceneEditsAllowedOnlyWhenStopped )
{
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::areSceneEditsAllowed( true ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::areSceneEditsAllowed( false ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, QuitPromptUsesSceneAndDocumentDirty )
{
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::needsQuitPrompt( false, 0 ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::needsQuitPrompt( true, 0 ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::needsQuitPrompt( false, 1 ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::needsQuitPrompt( true, 2 ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, NodeMoveDirtyOnlyAfterLayoutReady )
{
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( false, true ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( true, false ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( true, true ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, ToolSessionDirtyCombinesSources )
{
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::isToolSessionDirty( false, false, false ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::isToolSessionDirty( true, false, false ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::isToolSessionDirty( false, true, false ) );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::isToolSessionDirty( false, false, true ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, DocumentCoalesceKeyIsPerProperty )
{
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldCoalesceDocumentEdits( "material-prop:Albedo", "material-prop:Albedo" ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldCoalesceDocumentEdits( "material-prop:Albedo", "material-prop:Roughness" ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldCoalesceDocumentEdits( "", "material-prop:Albedo" ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldCoalesceDocumentEdits( "material-prop:Albedo", "" ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, RestoreClearsDirtyWhenMatchingLastSave )
{
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::shouldClearDocumentDirtyOnRestore( true ) );
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::shouldClearDocumentDirtyOnRestore( false ) );
}

SW_TEST_CASE( EditorSessionPolicyTest, PrefabIsolationDoesNotRequireCleanScene )
{
	SW_EXPECT_FALSE( sw::editor::EditorSessionPolicy::requiresCleanSceneForPrefabIsolation() );
}

SW_TEST_CASE( EditorSessionPolicyTest, DocumentTextSpanStoresOnlyChangedMiddle )
{
	const sw::string						 before = R"({"albedo":"white","roughness":0.5,"name":"Mat"})";
	const sw::string						 after	= R"({"albedo":"white","roughness":0.8,"name":"Mat"})";
	const sw::editor::EditorDocumentTextSpan span	= sw::editor::EditorSessionPolicy::makeDocumentTextSpan( before, after );
	SW_EXPECT_TRUE( span.removed == "5" );
	SW_EXPECT_TRUE( span.added == "8" );
	SW_EXPECT_TRUE( span.prefixLength + span.suffixLength + span.removed.size() == before.size() );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::reconstructDocumentTextFromAfter( span, after ) == before );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::reconstructDocumentTextFromBefore( span, before ) == after );
}

SW_TEST_CASE( EditorSessionPolicyTest, DocumentTextSpanFirstEditReversesLaterAfter )
{
	const sw::string						 before0 = R"({"field":"x"})";
	const sw::string						 after1	 = R"({"field":"xy"})";
	const sw::string						 afterN	 = R"({"field":"xyz"})";
	const sw::editor::EditorDocumentTextSpan firstSpan =
		sw::editor::EditorSessionPolicy::makeDocumentTextSpan( before0, after1 );
	SW_EXPECT_TRUE( sw::editor::EditorSessionPolicy::reconstructDocumentTextFromAfter( firstSpan, afterN ) == before0 );
}
