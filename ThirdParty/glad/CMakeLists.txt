# ==============================================================================
# @file ThirdParty/glad/CMakeLists.txt
# @brief GLAD — OpenGL 로더 (ATTACH_GLOBAL: sw_third_party_includes에 연결)
# ==============================================================================

sw_addVcpkgConfigLib(NAME glad CONFIG_TARGET glad::glad HEADER_ONLY ATTACH_GLOBAL)
