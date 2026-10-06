# ==============================================================================
# @file ThirdParty/glad/CMakeLists.txt
# @brief GLAD — OpenGL 로더 (ATTACH_GLOBAL: sw_third_party_includes에 연결)
# ==============================================================================

sw_addVcpkgPackage(NAME glad CONFIG_TARGET glad::glad ATTACH_GLOBAL)
