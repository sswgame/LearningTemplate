/**
 * @file RHITypes.h
 * @brief RHI 공통 형식의 묶음 헤더입니다. 핸들(`RHIHandles.h`), 열거형(`RHIEnums.h`), 서술체(`RHITextureDesc.h` · `RHIRenderPassInfo.h` 등)와 계약 상수(`RHIConstants.h`)를 모두 들입니다.
 * @details 지금은 옛 이름을 쓰는 곳을 위해 남긴 묶음이다. 새 코드는 필요한 작은 헤더를 직접 include 한다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHIBufferDesc.h"
#include "Engine/Graphics/RHI/RHIConstants.h"
#include "Engine/Graphics/RHI/RHIDeviceDesc.h"
#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIIndirectCommand.h"
#include "Engine/Graphics/RHI/RHIRenderPassInfo.h"
#include "Engine/Graphics/RHI/RHIScissorRect.h"
#include "Engine/Graphics/RHI/RHISwapChainDesc.h"
#include "Engine/Graphics/RHI/RHITextureDesc.h"
#include "Engine/Graphics/RHI/RHITransferTicket.h"
#include "Engine/Graphics/RHI/RHITransition.h"
#include "Engine/Graphics/RHI/RHIViewport.h"
