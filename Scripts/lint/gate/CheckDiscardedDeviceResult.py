#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""그래픽스 장치 호출(Vulkan `vk*` · D3D11 · D3D12 · DXGI · DXC)이 돌려준 결과(VkResult · HRESULT)를 받지 않고 버리는 문장을 잡는다.

결과는 `isVkSuccess` · `requireVk`(`Engine/Graphics/RHI/Vulkan/VulkanResultCheck.h`), `isHrSuccess` · `requireHr`
(`Engine/Graphics/RHI/DX/RHIDxResultCheck.h`)로 받거나 `if ( FAILED( … ) )` · 변수로 받아 본다. 버리면 실패한 생성 · 바인드 · 닫기 뒤에도
빈 핸들 · 닫히지 않은 리스트로 이어 가 엉뚱한 곳(검증 오류 · 장치 제거 · 검은 화면)에서 드러난다.

형식으로 막지 못하는 틈이라 게이트로 둔다: `VkResult` 는 SDK 의 enum, `HRESULT` 는 `long` 이고 `vk*` 함수 · COM 메서드 선언은 SDK 헤더라
`[[nodiscard]]` 를 붙일 수 없다. 래퍼만 두면 래퍼를 거치지 않은 직접 호출을 컴파일러가 모른다.

보는 것은 **문장의 맨 앞에서 부른 호출**뿐이다(앞 줄이 `;` · `{` · `}` · `)` · `else` · `:` 로 끝나는 줄). 식 안(`if ( … )`, `= …`, 인자)에서
부른 것은 결과를 쓰는 것이다. D3D 메서드는 `->` 로 부른 것만 본다(`ComPtr::Reset()` 은 `.` 이라 아니다). D3D12 의 void 생성 함수(뷰 · 샘플러)와
디버그 이름(`SetName`)은 보지 않는다 — 앞은 결과가 없고 뒤는 실패해도 진단 이름만 빠진다.

둘째 규칙: D3D 장치 메서드(생성 · Map · Close · Reset · Signal · Present · ResizeBuffers · GetBuffer)를 `SUCCEEDED( … )` · `FAILED( … )` 로 **바로**
감싸지 않는다. 그 모양은 실패하면 오류 없이 건너뛰거나 돌아가기 쉽다(`if ( SUCCEEDED( Map ) ) { 쓰기 }` — 업로드가 조용히 빠진다). `isHrSuccess`
(실패 이유를 남긴다) 로 받거나 HRESULT 를 이름 붙인 변수로 받아 실패 갈래에서 값을 남긴다. 선택 기능 질의(`CheckFeatureSupport` · `As` · `GetDesc*`)는
실패가 "없다" 는 정상 응답이라 보지 않는다.

  python Scripts/lint/gate/CheckDiscardedDeviceResult.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile  # noqa: E402

_kListScanRoot = ("Source/Engine/Graphics", "Source/Editor/Common/Backend")
_kSuffixes = (".h", ".hpp", ".inl", ".cpp")

# VkResult 를 돌려주는 Vulkan 함수(엔진이 쓰는 것). 새로 쓰는 함수가 VkResult 를 돌려주면 여기에 더한다.
_kVkCallRe = re.compile(
    r"^\s*vk(?:Create\w+|Allocate\w+|MapMemory|Bind\w*Memory\w*|QueueSubmit\w*|QueuePresentKHR|QueueWaitIdle|DeviceWaitIdle"
    r"|BeginCommandBuffer|EndCommandBuffer|Reset(?:CommandBuffer|CommandPool|Fences|DescriptorPool|Event)|WaitForFences|WaitSemaphores"
    r"|GetFenceStatus|AcquireNextImage\w*|GetSwapchainImagesKHR|Enumerate\w+|GetPipelineCacheData|GetQueryPoolResults|FreeDescriptorSets"
    r"|GetPhysicalDeviceSurface\w+KHR|SetDebugUtilsObjectNameEXT)\s*\("
)
# HRESULT 를 돌려주는 D3D · DXGI · DXC 메서드(`->` 로 부른 것만).
_kDxCallRe = re.compile(
    r"^\s*[\w.\[\]()\->:]*?->(?P<method>Create\w+|Close|Reset|Map|Present1?|Signal|SetEventOnCompletion|QueryInterface"
    r"|ResizeBuffers|GetBuffer|CheckFeatureSupport|GetData|LoadFile|Compile)\s*\("
)
# D3D12 에서 void 를 돌려주는 생성 함수 — DX12 파일에서만 뺀다(D3D11 의 같은 이름은 HRESULT 다).
_kD3D12VoidRe = re.compile(r"^Create(?:RenderTarget|DepthStencil|ShaderResource|UnorderedAccess|ConstantBuffer)View$|^CreateSampler$")
# 둘째 규칙: 장치 메서드를 SUCCEEDED · FAILED 로 바로 감싼 것(선택 기능 질의는 빼고).
_kWrappedDxCallRe = re.compile(
    r"\b(?P<macro>SUCCEEDED|FAILED)\(\s*[\w.\[\]()\->:]*?->(?P<method>Create\w+|Close|Reset|Map|Present1?|Signal|ResizeBuffers|GetBuffer)\s*\("
)
_kStatementEndRe = re.compile(r"(?:[;{})]|\belse|:)\s*$")


def _isDx12File(relative: str) -> bool:
    return "/DX12/" in relative or "DX12" in Path(relative).name


def findDiscardedDeviceResults(source: SourceFile) -> list[str]:
    """문장 맨 앞에서 부르고 결과를 버린 장치 호출 줄을 위반 문자열로 돌려줍니다."""
    relative = source.relative
    listViolation: list[str] = []
    previousCode = "{"
    for lineIndex, line in enumerate(source.listMaskedLine, start=1):
        stripped = line.strip()
        if not stripped:
            continue
        bStatementStart = _kStatementEndRe.search(previousCode) is not None or previousCode.startswith("#")
        previousCode = stripped
        for wrapped in _kWrappedDxCallRe.finditer(line):
            method = wrapped.group("method")
            if _isDx12File(relative) and _kD3D12VoidRe.match(method) is not None:
                continue
            listViolation.append(f"{relative}:{lineIndex}: {wrapped.group('macro')}( {method} )  | {source.listLine[lineIndex - 1].strip()}")
        if bStatementStart == False or stripped.startswith("return"):
            continue
        name = ""
        if _kVkCallRe.match(line) is not None:
            name = stripped.split("(", 1)[0].strip()
        else:
            match = _kDxCallRe.match(line)
            if match is not None:
                method = match.group("method")
                if _isDx12File(relative) and _kD3D12VoidRe.match(method) is not None:
                    continue
                name = method
        if name:
            listViolation.append(f"{relative}:{lineIndex}: {name}  | {source.listLine[lineIndex - 1].strip()}")
    return listViolation


class CheckDiscardedDeviceResultGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "그래픽스 장치 호출의 VkResult · HRESULT 를 버리지 않고 isVkSuccess · isHrSuccess 등으로 받는지 검사"
    buildComment = "Checking that graphics device call results (VkResult/HRESULT) are not discarded..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 장치 호출 결과 버림"
    violationHeader = "VkResult · HRESULT 를 버린 장치 호출"
    hint = (
        "  결과를 받아 봅니다. 이어 갈 수 있으면 [[nodiscard]] 인 isVkSuccess · isHrSuccess(오류 로그 + false),\n"
        "  이어 갈 수 없으면 requireVk · requireHr(SW_FATAL) 입니다:\n"
        "      if ( isVkSuccess( vkBindImageMemory( device, image, memory, 0 ), \"vkBindImageMemory\" ) == false ) return 0;\n"
        "      requireHr( list->Close(), \"ID3D12GraphicsCommandList::Close\" );\n"
        "  실패가 정상 응답인 선택 기능이면 if ( FAILED( … ) ) 로 받고 그 뜻을 주석으로 적습니다."
    )
    selfTestCases = [
        {
            "name": "Vulkan 메모리 바인드 결과 버림",
            "expectedViolation": "Source/Engine/Graphics/RHI/Vulkan/Probe.cpp:3: vkBindImageMemory",
            "files": {
                "Source/Engine/Graphics/RHI/Vulkan/Probe.cpp": (
                    "void probe( VkDevice d, VkImage i, VkDeviceMemory m )\n"
                    "{\n"
                    "    vkBindImageMemory( d, i, m, 0 );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "D3D12 리스트 닫기 결과 버림",
            "expectedViolation": "Source/Engine/Graphics/RHI/DX12/Probe.cpp:3: Close",
            "files": {
                "Source/Engine/Graphics/RHI/DX12/Probe.cpp": (
                    "void probe( ID3D12GraphicsCommandList* pList )\n"
                    "{\n"
                    "    pList->Close();\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "D3D11 렌더 타깃 뷰 생성 결과 버림(D3D11 은 HRESULT)",
            "expectedViolation": "Source/Engine/Graphics/RHI/DX11/Probe.cpp:3: CreateRenderTargetView",
            "files": {
                "Source/Engine/Graphics/RHI/DX11/Probe.cpp": (
                    "void probe( ID3D11Device* pDevice, ID3D11Resource* pRes, ID3D11RenderTargetView** ppRtv )\n"
                    "{\n"
                    "    pDevice->CreateRenderTargetView( pRes, nullptr, ppRtv );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "Map 실패를 SUCCEEDED 로 감싸 조용히 건너뜀",
            "expectedViolation": "Source/Engine/Graphics/RHI/DX11/ProbeMap.cpp:4: SUCCEEDED( Map )",
            "files": {
                "Source/Engine/Graphics/RHI/DX11/ProbeMap.cpp": (
                    "void probe( ID3D11DeviceContext* pContext, ID3D11Buffer* pBuffer, D3D11_MAPPED_SUBRESOURCE& mapped )\n"
                    "{\n"
                    "    // 실패하면 아무것도 쓰지 않고 지나간다\n"
                    "    if ( SUCCEEDED( pContext->Map( pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )\n"
                    "        pContext->Unmap( pBuffer, 0 );\n"
                    "}\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source/Engine/Graphics · Source/Editor/Common/Backend 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findDiscardedDeviceResults(source))


main = CheckDiscardedDeviceResultGate.run


if __name__ == "__main__":
    sys.exit(main())
