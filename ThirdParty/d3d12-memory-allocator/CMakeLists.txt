# ==============================================================================
# @file ThirdParty/d3d12-memory-allocator/CMakeLists.txt
# @brief D3D12 Memory Allocator(GPUOpen, MIT) — DX12 백엔드의 장치 메모리 할당기. x64-windows 트리플릿(동적)은 D3D12MA.dll 이다 —
#        Dev 는 RHI_DX12 의 스테이지 applocal, 배포는 App 의 applocal, ASan 은 설치 트리 bin 복사가 Bin 에 두고 패키지는 Bin 의 공유 라이브러리를 담는다(Jolt.dll 과 같은 길).
#        헤더는 Source/Engine/Graphics/RHI/DX/DX12 에서만 include 한다. DX12 백엔드의 그래픽 라이브러리(`sw_graphics_dx12_libs`)에 잇는다.
#        전용 서버는 RHI 를 짓지 않으므로 찾지 않는다.
# ==============================================================================

if(NOT WIN32 OR SW_TARGET_TYPE STREQUAL "Server")
	return()
endif()

sw_add_vcpkg_package(
	NAME d3d12-memory-allocator
	PACKAGE D3D12MemoryAllocator
	CONFIG_TARGET GPUOpen::D3D12MemoryAllocator
)
# vcpkg 포트는 D3D12MA_USING_DIRECTX_HEADERS 를 싣고 `directx/d3d12.h` · `dxguids.h` 를 include 하게 한다. 엔진은 Windows SDK 의 d3d12.h 를 쓰고
# (EnginePlatformHeaders.h) 두 판은 같은 가드라 섞으면 dxguids 의 IID 단언이 깨진다 — "헤더를 먼저 include 했다" 로 알린다(D3D12MemAlloc.h 머리의 공식 구성).
# 할당기 공개 형식의 크기 · 배치는 d3d12.h 판에 달리지 않는다.
target_compile_definitions(d3d12-memory-allocator INTERFACE D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED)
target_link_libraries(sw_graphics_dx12_libs INTERFACE d3d12-memory-allocator)
