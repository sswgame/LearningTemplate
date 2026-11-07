#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Panels/Settings/Model/PreferencesModel.h"

#include "Engine/Module/ModuleCatalog.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /**
     * @brief 값 있는 섹션 하나와 그리기 함수 페이지 하나를 등록부에 잠시 올립니다. 값은 프로퍼티가 둘인 `ModuleOverride`(`_name` · `_bEnabled`)를 빌려 씁니다.
     * @details 항목을 등록자 없이 목록에 바로 올린다 — 등록자의 떼기는 진짜 환경설정 파일에 쓴다(테스트는 그 파일에 쓰지 않는다). 파일 값을 입힌 것으로 두어 파일을 읽지도 않는다.
     *          저장을 강제하는 `savePendingChanges( true )` 도 같은 까닭에 기다리는 편집이 있을 때는 부르지 않는다.
     */
    struct PreferencesModelTest
    {
        static inline int32 s_changedCount = 0;

        ModuleOverride             _instance;
        ModuleOverride             _default;
        EditorSettingsRegistration _section;
        EditorSettingsRegistration _page;
        PreferencesModel           _model;

        PreferencesModelTest()
            : _instance{
        }
            , _default{}
            , _section{ { "zztest.section", 1000000 }, "ZzTest/Section", &ModuleOverride::StaticType, &_instance, &_default, &PreferencesModelTest::onChanged, nullptr, SW_TRUE },
            _page{ { "zztest.page", 1000001 }, "ZzTest/Page", &EditorSettingsPageNone::getType, nullptr, nullptr, nullptr, &PreferencesModelTest::drawPage, SW_TRUE },
            _model{}
        {
            s_changedCount                        = 0;
            EditorExtensionRegistrationList& list = EditorExtensionRegistry<EditorSettingsRegistration>::getList();
            SW_EXPECT_TRUE( list.addRegistration( _section ) );
            SW_EXPECT_TRUE( list.addRegistration( _page ) );
        }

        ~PreferencesModelTest()
        {
            EditorExtensionRegistrationList& list = EditorExtensionRegistry<EditorSettingsRegistration>::getList();
            list.removeRegistration( _page );
            list.removeRegistration( _section );
        }

        static void onChanged() { ++s_changedCount; }
        static void drawPage() {}

        static bool contains( const vector<const EditorSettingsRegistration*>& listSection, const EditorSettingsRegistration& section )
        {
            for ( const EditorSettingsRegistration* pSection : listSection )
            {
                if ( pSection == &section )
                    return true;
            }
            return false;
        }
    };
} // namespace

SW_TEST_CASE_F( PreferencesModelTest, EmptyFilterShowsEverySectionAndSelectsOne )
{
    const vector<const EditorSettingsRegistration*>& listVisible = _model.refreshVisibleSections( "" );

    SW_EXPECT_EQUAL( EditorExtensionRegistry<EditorSettingsRegistration>::getCount(), _model.getVisibleSectionCount() );
    SW_EXPECT_TRUE( contains( listVisible, _section ) );
    SW_EXPECT_TRUE( contains( listVisible, _page ) );
    SW_EXPECT_TRUE( _model.getSelected() == listVisible.front() );
}

SW_TEST_CASE_F( PreferencesModelTest, FilterMatchesLabelCaseInsensitively )
{
    const vector<const EditorSettingsRegistration*>& listVisible = _model.refreshVisibleSections( "zztest/section" );

    SW_REQUIRE_EQUAL( 1u, _model.getVisibleSectionCount() );
    SW_EXPECT_TRUE( listVisible.front() == &_section );
    SW_EXPECT_TRUE( _model.getSelected() == &_section );
}

SW_TEST_CASE_F( PreferencesModelTest, FilterMatchesPropertyNameOfValueSectionOnly )
{
    const vector<const EditorSettingsRegistration*>& listVisible = _model.refreshVisibleSections( "_bEnabled" );

    SW_EXPECT_TRUE( contains( listVisible, _section ) );
    SW_EXPECT_FALSE( contains( listVisible, _page ) ); // 그리기 함수 쪽은 이름으로만 찾는다
}

SW_TEST_CASE_F( PreferencesModelTest, NoMatchSelectsNothing )
{
    (void)_model.refreshVisibleSections( "" );
    SW_EXPECT_NOT_NULL( _model.getSelected() );

    (void)_model.refreshVisibleSections( "no section is named this" );

    SW_EXPECT_EQUAL( 0u, _model.getVisibleSectionCount() );
    SW_EXPECT_NULL( _model.getSelected() );
}

SW_TEST_CASE_F( PreferencesModelTest, SelectionStaysWhileVisibleAndMovesWhenFilteredOut )
{
    (void)_model.refreshVisibleSections( "zztest" );
    _model.selectSection( _page );
    (void)_model.refreshVisibleSections( "ZZTEST/" );
    SW_EXPECT_TRUE( _model.getSelected() == &_page );

    (void)_model.refreshVisibleSections( "zztest/section" );
    SW_EXPECT_TRUE( _model.getSelected() == &_section );
}

SW_TEST_CASE_F( PreferencesModelTest, SectionRemovedByModuleUnloadIsNotHeld )
{
    (void)_model.refreshVisibleSections( "zztest/section" );
    SW_EXPECT_TRUE( _model.getSelected() == &_section );

    EditorExtensionRegistry<EditorSettingsRegistration>::getList().removeRegistration( _section );
    (void)_model.refreshVisibleSections( "zztest/section" ); // 검색 글은 같고 등록 세대만 바뀌었다

    SW_EXPECT_NULL( _model.getSelected() );
    SW_EXPECT_EQUAL( 0u, _model.getVisibleSectionCount() );
}

SW_TEST_CASE_F( PreferencesModelTest, ModifiedOnlyHidesPropertiesAtDefault )
{
    vector<hashed_string> listHidden;
    _model.selectSection( _section );

    _model.collectHiddenProperties( listHidden );
    SW_EXPECT_TRUE( listHidden.empty() ); // 체크칸을 켜지 않았다

    _model.editModifiedOnly() = true;
    _model.collectHiddenProperties( listHidden );
    SW_EXPECT_EQUAL( size_t{ 2 }, listHidden.size() );

    _instance._name = "changed";
    _model.collectHiddenProperties( listHidden );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listHidden.size() );
    SW_EXPECT_TRUE( listHidden[0] == hashed_string( "_bEnabled" ) );
}

SW_TEST_CASE_F( PreferencesModelTest, ModifiedOnlyHidesNothingOnAPageOrWithoutSelection )
{
    vector<hashed_string> listHidden;
    _model.editModifiedOnly() = true;

    _model.collectHiddenProperties( listHidden );
    SW_EXPECT_TRUE( listHidden.empty() );

    _model.selectSection( _page );
    _model.collectHiddenProperties( listHidden );
    SW_EXPECT_TRUE( listHidden.empty() );
}

SW_TEST_CASE_F( PreferencesModelTest, EditCallsTheHookAndWaitsBeforeSaving )
{
    _model.savePendingChanges( true ); // 기다리는 것이 없으면 쓰지 않는다
    SW_EXPECT_FALSE( _model.hasPendingSave() );

    _model.selectSection( _section );
    _model.recordEdited();
    SW_EXPECT_EQUAL( 1, s_changedCount );
    SW_EXPECT_TRUE( _model.hasPendingSave() );

    _model.savePendingChanges( false ); // 시간이 지나지 않았다 — 끌기 한 번을 한 번의 쓰기로 묶는다
    SW_EXPECT_TRUE( _model.hasPendingSave() );
}

SW_TEST_CASE_F( PreferencesModelTest, EditWithoutHookStillWaitsToSave )
{
    _model.selectSection( _page );
    _model.recordEdited();
    SW_EXPECT_EQUAL( 0, s_changedCount );
    SW_EXPECT_TRUE( _model.hasPendingSave() );
}

SW_TEST_CASE_F( PreferencesModelTest, ResetRestoresDefaultsAndCallsTheHookOnce )
{
    _instance._name     = "changed";
    _instance._bEnabled = false;
    _model.selectSection( _section );

    _model.resetSelectedSection();

    SW_EXPECT_TRUE( _instance._name == _default._name );
    SW_EXPECT_TRUE( _instance._bEnabled == _default._bEnabled );
    SW_EXPECT_EQUAL( 1, s_changedCount );
    SW_EXPECT_TRUE( _model.hasPendingSave() );
}

SW_TEST_CASE_F( PreferencesModelTest, ResetWithoutSelectionDoesNothing )
{
    _model.resetSelectedSection();
    SW_EXPECT_EQUAL( 0, s_changedCount );
    SW_EXPECT_FALSE( _model.hasPendingSave() );
}
