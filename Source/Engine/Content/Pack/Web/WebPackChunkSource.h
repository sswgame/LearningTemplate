/**
 * @file WebPackChunkSource.h
 * @brief 웹의 청크 받기 — 페이지 옆 `Packs/<팩>` 을 fetch 해 가상 파일 시스템의 팩 폴더에 씁니다(`IPackChunkSource`).
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "Engine/Content/Pack/PackChunkLoader.h"

namespace sw
{
    /** @brief 웹 받기를 만듭니다(Emscripten fetch — 링크 `-sFETCH=1`). */
    unique_ptr<IPackChunkSource> makeWebPackChunkSource();
} // namespace sw
