# ==============================================================================
# @file Source/Engine/Graphics/RHI/Modules/CMakeLists.txt
# @brief RHI 백엔드 MODULE DLL 플러그인 (Dev 전용 — Shipping 은 Source/Engine/CMakeLists.txt 가 백엔드 하나를 Engine 에 정적 링크)
# ==============================================================================

# 백엔드 목록은 CookContract.json 의 rhi_backends 다. 이 플랫폼에 없는 백엔드(매니페스트 _listPlatform — DX 는 Windows 만)는
# 모듈 함수가 "짓지 않는다" 로 적고(모듈 폴더 · 장치 소스) 건너뛴다.
foreach(backendName IN LISTS SW_RHI_BACKEND_NAMES)
	add_subdirectory(${SW_RHI_BACKEND_${backendName}_SOURCE_FOLDER})
endforeach()
