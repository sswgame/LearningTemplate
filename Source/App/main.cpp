#include "pch.h"

#include "App/AppEntry.h"

#if defined( SW_PLATFORM_WINDOWS )
// 하이브리드 노트북의 드라이버가 이 실행 파일을 고성능(외장) GPU 로 돌린다 — NVIDIA Optimus · AMD 전환 그래픽 둘 다(제조사와 무관).
// GL 은 어댑터를 고르는 API 가 없어 이 값만 따른다. DX · Vulkan 은 따로 OS 선호 순서로 고른다(RHIDxgiAdapter.h, gv_rhiPreferMinimumPower). 언리얼 · 유니티 실행 파일도 내보낸다.
extern "C"
{
    __declspec( dllexport ) unsigned long NvOptimusEnablement                  = 1;
    __declspec( dllexport ) int           AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_WEB )
int32 main( int32 argc, utf8* pArgv[] )
{
    return sw::AppEntry::run( argc, pArgv );
}
#elif defined( SW_PLATFORM_ANDROID )
// Android 는 `main` 이 없다 — NativeActivity 가 `android_main` 을 부른다(App/Android/AndroidMain.cpp).
#else
    #error "Unsupported platform"
#endif
