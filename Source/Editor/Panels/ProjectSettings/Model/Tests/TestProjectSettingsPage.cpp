#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsPage.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( ProjectSettingsPageTest, HasDirtyLooksAtEveryPage )
{
    vector<unique_ptr<ProjectSettingsPage>> listPage;
    SW_EXPECT_FALSE( ProjectSettingsPage::hasDirty( listPage ) );
    listPage.push_back( make_unique<ProjectSettingsPage>() );
    listPage.push_back( make_unique<ProjectSettingsPage>() );
    SW_EXPECT_FALSE( ProjectSettingsPage::hasDirty( listPage ) );
    listPage[1]->_bDirty = true;
    SW_EXPECT_TRUE( ProjectSettingsPage::hasDirty( listPage ) );
}

// 형식을 찾지 못하면 열었다고 표시하되 형식 없음으로 남고, 표시를 켜지 않는다.
SW_TEST_CASE( ProjectSettingsPageTest, OpeningAnUnknownTypeClearsTypeFound )
{
    ProjectSettingsPage page;
    page._entry._typeName = "NoSuchSettingsType";
    page._bTypeFound      = true;
    page.open();
    SW_EXPECT_TRUE( page._bOpened );
    SW_EXPECT_FALSE( page._bTypeFound );
    SW_EXPECT_FALSE( page._bDirty );
}
