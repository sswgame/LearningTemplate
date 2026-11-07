#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Runner/AutomationPerBackend.h"

// AutomationPerBackendTest — 백엔드 사이의 `<Record>` 대조(`findRecordMismatches`, 부모 App 가 자식 보고의 records[] 로 결정성을 판정하는 근거)와 백엔드 목록 읽기(`parseBackendList`)(nogpu).

/**
 * @brief [AutomationPerBackendTest] 모두 같으면 빈 목록이고, 다른 순번만 한 줄로 각 백엔드의 값을 적는다
 */
SW_TEST_CASE( AutomationPerBackendTest, RecordMismatchNamesRecordAndEachBackendValue )
{
    const sw::vector<sw::string> listSame{ "frame 300 Nile.Month = 3", "frame 300 Nile.Money = 120" };
    SW_EXPECT_TRUE( sw::AutomationPerBackend::findRecordMismatches( {
                                                                        {"dx11", listSame},
                                                                        {  "vk", listSame}
    } )
                        .empty() );

    const sw::vector<sw::string> listDifferent{ "frame 300 Nile.Month = 3", "frame 300 Nile.Money = 118" };
    const sw::vector<sw::string> listMismatch =
        sw::AutomationPerBackend::findRecordMismatches( {
            {"dx11",      listSame},
            {"dx12",      listSame},
            {  "vk", listDifferent}
    } );
    SW_EXPECT_EQUAL( size_t{ 1 }, listMismatch.size() );
    if ( listMismatch.size() == 1 )
        SW_EXPECT_EQUAL( sw::string( "record #2 'frame 300 Nile.Money': dx11=120 dx12=120 vk=118" ), listMismatch[0] );
}

/**
 * @brief [AutomationPerBackendTest] 백엔드 하나는 견줄 것이 없고, 기록 수가 다르면 없는 쪽을 (none) 으로, 이름이 다르면 줄 전체를 적는다
 */
SW_TEST_CASE( AutomationPerBackendTest, RecordMismatchReportsMissingAndRenamedLines )
{
    SW_EXPECT_TRUE( sw::AutomationPerBackend::findRecordMismatches( {
                                                                        { "dx12", { "frame 1 A = 1" } }
    } )
                        .empty() );

    const sw::vector<sw::string> listMismatch =
        sw::AutomationPerBackend::findRecordMismatches( {
            {"dx11", { "frame 1 A = 1", "frame 2 B = 2" }},
            {  "gl",                  { "frame 1 C = 1" }}
    } );
    SW_EXPECT_EQUAL( size_t{ 2 }, listMismatch.size() );
    if ( listMismatch.size() == 2 )
    {
        SW_EXPECT_EQUAL( sw::string( "record #1 'frame 1 A': dx11=1 gl='frame 1 C = 1'" ), listMismatch[0] );
        SW_EXPECT_EQUAL( sw::string( "record #2 'frame 2 B': dx11=2 gl=(none)" ), listMismatch[1] );
    }
}

/**
 * @brief [AutomationPerBackendTest] all 은 이 플랫폼의 검증 백엔드(쿠킹 표 scenario_backends — Windows 넷, 리눅스 vk · gl, 데스크톱 WebGPU 는 만들 수 있어도 아니다), 이름을 적은 백엔드는 플랫폼과 무관하게 표 순서로 고른다
 */
SW_TEST_CASE( AutomationPerBackendTest, BackendListAllFollowsPlatformAvailability )
{
    sw::vector<sw::string> listBackend;
    sw::string             error;
    SW_EXPECT_TRUE( sw::AutomationPerBackend::parseBackendList( "all", listBackend, error ) );
#if defined( SW_PLATFORM_WINDOWS )
    const sw::vector<sw::string> listExpectedAll{ "dx11", "dx12", "vk", "gl" };
#elif defined( SW_PLATFORM_LINUX )
    const sw::vector<sw::string> listExpectedAll{ "vk", "gl" };
#elif defined( SW_PLATFORM_ANDROID )
    const sw::vector<sw::string> listExpectedAll{ "vk" };
#elif defined( SW_PLATFORM_WEB )
    const sw::vector<sw::string> listExpectedAll{ "wgpu" };
#else
    #error "Unsupported platform"
#endif
    SW_EXPECT_TRUE( listBackend == listExpectedAll );

    SW_EXPECT_TRUE( sw::AutomationPerBackend::parseBackendList( "gl,dx12", listBackend, error ) );
    SW_EXPECT_TRUE( listBackend == ( sw::vector<sw::string>{ "dx12", "gl" } ) );
    SW_EXPECT_FALSE( sw::AutomationPerBackend::parseBackendList( "all,vk", listBackend, error ) );
}
