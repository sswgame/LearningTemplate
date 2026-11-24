/**
 * @file RHIPipelineCacheFile.h
 * @brief 드라이버 파이프라인 캐시(Vulkan `VkPipelineCache` · D3D12 `ID3D12PipelineLibrary`)의 디스크 파일 하나입니다 — 파일 이름, 주기 저장, 종료 저장의 규칙을 백엔드가 함께 씁니다.
 * @details 언리얼 `FVulkanPipelineCacheFile` · D3D12 드라이버 PSO 캐시 자리다. 캐시 객체는 백엔드가 가지고, 이 형식은 바이트를 언제 어디에 쓰는지만 안다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    /** @brief 캐시 전체를 바이트로 냅니다(워커 · 종료 스레드에서 불린다). 내지 못하면 오류를 남기고 false 입니다. */
    using RHIPipelineCacheSerializeDelegate = Delegate<bool( vector<uint8>& outBytes )>;

    /**
     * @class RHIPipelineCacheFile
     * @brief 파이프라인 캐시 파일 하나입니다. 파일 머리말 참고. 렌더 스레드에서 쓰고, 디스크를 쓰는 일은 워커가 한다(종료 저장은 부른 스레드).
     * @details 캐시 데이터의 머리는 만든 장치를 적고, 다른 장치 · 드라이버는 그 데이터를 버리고 빈 캐시로 시작한다(Vulkan 명세 `vkCreatePipelineCache`, D3D12 `CreatePipelineLibrary`).
     *          파일이 하나뿐이면 하드웨어 → software(lavapipe · WARP) → 하드웨어 실행이 하드웨어 캐시를 매번 덮어 처음부터 다시 만든다(R-13) — 장치 · 드라이버마다 파일을 가른다.
     */
    class SW_API RHIPipelineCacheFile
    {
    public:
        /** @brief `Saved` 아래의 상대 경로입니다(`ShaderCache/<prefix>_<vendor>_<device>_<driver>.bin`, 16 진). 드라이버 판은 64 비트다(D3D12 UMD 판). */
        static string makeRelativePath( string_view prefix, uint32 vendorID, uint32 deviceID, uint64 driverVersion );

        /**
         * @brief 주기 저장을 할 때인지 봅니다. 지난 저장(@p lastSaveNanoseconds) 뒤로 주기(@p intervalNanoseconds)가 지났고, 캐시 크기가 지난 저장 때와 다르면 true 입니다.
         * @details 크기만 묻는 것은 싸다. 새 파이프라인이 없으면 크기가 같아 디스크를 건드리지 않는다 — 쓰는 길은 바이트를 비교한다.
         */
        static constexpr bool isPeriodicSaveDue( int64 nowNanoseconds, int64 lastSaveNanoseconds, int64 intervalNanoseconds, uint64 cacheBytes, uint64 lastSavedBytes )
        {
            return intervalNanoseconds > 0 && nowNanoseconds - lastSaveNanoseconds >= intervalNanoseconds && cacheBytes != lastSavedBytes;
        }

        RHIPipelineCacheFile();
        RHIPipelineCacheFile( const RHIPipelineCacheFile& )            = delete;
        RHIPipelineCacheFile& operator=( const RHIPipelineCacheFile& ) = delete;

        /**
         * @brief 파일을 정하고 지금 내용을 읽습니다(없거나 못 읽으면 빈 바이트 — 파이프라인을 다시 만들 뿐이다). 장치를 만들 때 한 번 부릅니다.
         * @param saveIntervalSeconds 주기 저장 간격(`RHIDeviceDesc::_pipelineCacheSaveIntervalSeconds`, 0 보다 크다).
         * @param serialize 캐시를 바이트로 내는 함수입니다. `saveAndClose` 가 끝날 때까지 부를 수 있어야 한다.
         */
        void open( const AbsolutePath& path, float64 saveIntervalSeconds, RHIPipelineCacheSerializeDelegate serialize, vector<uint8>& outBytes );
        /** @brief 파일을 열었으면 true 입니다. */
        bool isOpen() const { return _serialize.isBound(); }
        /** @brief 지난 저장 뒤로 주기가 지났으면 true 입니다 — 이때만 캐시 크기를 묻습니다(`saveIfDue`). */
        bool isIntervalPassed() const;
        /**
         * @brief 주기가 지났고 캐시(@p cacheBytes)가 자랐으면 워커에 저장을 맡깁니다(R-13 — 실행 중에 죽어도 주기 앞까지의 컴파일이 남는다). 프레임 시작이 부른다.
         * @details 캐시 데이터를 뜨고 디스크를 읽고 쓰는 일은 렌더 스레드에서 하지 않는다 — 캐시가 클수록 그 프레임이 늦는다. 서비스(작업 풀)가 없는 도구는 종료 저장만 한다.
         */
        void saveIfDue( uint64 cacheBytes );
        /** @brief 맡긴 저장을 기다린 뒤 마지막으로 한 번 쓰고 닫습니다. 캐시 객체를 지우기 전에 부릅니다. 열지 않았으면 아무것도 하지 않습니다. */
        void saveAndClose();

    private:
        /** @brief 캐시를 디스크에 씁니다. 디스크의 바이트와 같으면 쓰지 않습니다. */
        void write();

    private:
        AbsolutePath                      _path;
        RHIPipelineCacheSerializeDelegate _serialize;
        int64                             _saveIntervalNanoseconds;
        int64                             _lastSaveNanoseconds; ///< 지난 저장(또는 읽기) 시각
        uint64                            _lastSavedBytes;      ///< 지난 저장(또는 읽기) 때의 캐시 크기
        TaskStageHandle                   _saveStage;           ///< 주기 저장 태스크(닫기 전에 기다린다)
    };
} // namespace sw
