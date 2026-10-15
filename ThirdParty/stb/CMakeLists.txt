# ==============================================================================
# @file ThirdParty/stb/CMakeLists.txt
# @brief stb — 헤더 하나짜리 이미지(stb_image) · 오디오(stb_vorbis) 디코더. CMake 설정이 없어 vcpkg include 를 붙인 INTERFACE 타깃으로 쓴다.
#        stb 를 include 하는 타깃은 이 타깃을 직접 링크한다 — 다른 vcpkg 타깃(cgltf · 그래픽스 라이브러리)이 우연히 같은 include 폴더를
#        끌어오는 데 기대면, 그 타깃이 없는 구성(게임을 빼고 짓는 CI · DX12 Shipping · 전용 서버)에서만 `stb_image.h` 를 못 찾는다.
# ==============================================================================

sw_addVcpkgHeaderOnly(NAME stb PROBE stb_image.h)
