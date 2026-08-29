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
