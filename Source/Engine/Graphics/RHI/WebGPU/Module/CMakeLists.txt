# ==============================================================================
# @file Source/Engine/Graphics/RHI/WebGPU/Module/CMakeLists.txt
# @brief RHI_WebGPU MODULE — WebGPU 백엔드. 웹은 배포 정적 링크(`SW_SHIPPING_RHI_BACKEND=WebGPU`)로 짓고, 이 MODULE 은 Dawn 으로 데스크톱에서 돌릴 때 쓴다
# ==============================================================================

sw_add_rhi_backend_module(WebGPU)
