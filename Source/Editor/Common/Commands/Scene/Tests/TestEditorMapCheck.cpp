// EditorMapCheckTest — 맵 검사 등록점: 등록된 검사의 결과가 검사 id 출처로 표에 남고, 다시 돌리면 그 검사의 결과만 바뀌며, 오브젝트 결과와 섞이지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Scene/EditorMapCheck.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorMapCheckTestInternal
    {
        static constexpr const utf8* kCheckID = "zztest.mapcheck";

        static inline uint32 s_issueCount = 0;

        static void collectTestIssues( const GameObjectManager& /*manager*/, EditorMapCheckReport& outReport )
        {
            for ( uint32 index = 0; index < s_issueCount; ++index )
            {
                outReport.add( ValidationSeverity::Warning, "zztest/data.xml", "test issue" );
            }
        }

        /** @brief 검사 하나를 등록부에 잠시 올립니다. */
        struct ScopedCheck
        {
            EditorMapCheckRegistration _registration;

            ScopedCheck()
                : _registration{
                      { kCheckID, 1000000 },
                      &EditorMapCheckTestInternal::collectTestIssues
            }
            {
                SW_EXPECT_TRUE( EditorExtensionRegistration::addTo( EditorExtensionRegistry<EditorMapCheckRegistration>::getList(), _registration ) );
            }
            ~ScopedCheck() { EditorExtensionRegistration::removeFrom( EditorExtensionRegistry<EditorMapCheckRegistration>::getList(), _registration ); }
            ScopedCheck( const ScopedCheck& )            = delete;
            ScopedCheck& operator=( const ScopedCheck& ) = delete;
        };

        static uint32 countIssues( GameObjectManager& manager )
        {
            vector<ValidationIssue> listIssue;
            manager.getSubsystem<SceneValidationIssues>().getLog().collectIssues( listIssue );
            uint32 count = 0;
            for ( const ValidationIssue& issue : listIssue )
            {
                count += issue._sourceID == EditorMapCheck::makeSourceID( kCheckID ) ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace

SW_TEST_CASE( EditorMapCheckTest, RegisteredCheckReplacesOnlyItsIssues )
{
    using Internal = EditorMapCheckTestInternal;
    GameObjectManager           manager;
    const Internal::ScopedCheck check;

    Internal::s_issueCount = 2;
    SW_EXPECT_TRUE( EditorMapCheck::runAll( manager ) >= 2u ); // 다른 등록된 검사의 결과가 함께 있을 수 있다
    SW_EXPECT_EQUAL( 2u, Internal::countIssues( manager ) );
    Internal::s_issueCount = 1;
    (void)EditorMapCheck::runAll( manager );
    SW_EXPECT_EQUAL( 1u, Internal::countIssues( manager ) );
    Internal::s_issueCount = 0;
    (void)EditorMapCheck::runAll( manager );
    SW_EXPECT_EQUAL( 0u, Internal::countIssues( manager ) );
}

SW_TEST_CASE( EditorMapCheckTest, CheckSourceIsNotAnObject )
{
    SW_EXPECT_TRUE( EditorMapCheck::isCheckSource( EditorMapCheck::makeSourceID( "catalog.files" ) ) );
    SW_EXPECT_FALSE( EditorMapCheck::isCheckSource( 17u ) );
    SW_EXPECT_TRUE( EditorMapCheck::makeSourceID( "catalog.files" ) != EditorMapCheck::makeSourceID( "housing.partPrefabs" ) );
}
