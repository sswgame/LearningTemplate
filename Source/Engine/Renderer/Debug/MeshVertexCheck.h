/**
 * @file MeshVertexCheck.h
 * @brief 그리는 메시의 GPU 정점(렌더 프록시 · 그릴 때 거는 버퍼)을 CPU 정점과 바이트로 견주는 개발 전용 진단입니다(`MeshVertexCheck`).
 * @details 골든이 어긋나면 자동화가 요청한다(`RenderDebugRegistry::requestMeshVertexCheck`). 한 번 깨진 정점은 그 메시가 사라질 때까지 그림에 남으므로, 어긋난 자리에서 되읽으면
 *          CPU 사본이 틀렸는지 · 프록시가 틀렸는지 · 풀 복사가 틀렸는지 갈린다. 결과는 로그다. 배포 구성(`SW_SHIPPING`)의 렌더러는 이것을 부르지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"

namespace sw
{
    class GPUScene;
    class IRHICommandList;
    class IRHIDevice;
    class RenderDebugRegistry;

    /**
     * @class MeshVertexCheck
     * @brief 파일 머리말 참고. 렌더 스레드에서만 쓴다. CPU 정점을 쥔(`MeshCPUAccess::Keep`) 메시만 견준다.
     */
    class SW_API MeshVertexCheck
    {
    public:
        /** @brief 두 정점 사본([위치 구간][속성 구간] — `RHIMeshVertexStreams`)의 첫 차이입니다. */
        struct Difference
        {
            uint32 _byteOffset{ 0 };         ///< 처음 다른 바이트(사본 시작부터)
            uint32 _vertexIndex{ 0 };        ///< 그 바이트가 든 정점
            uint32 _differingByteCount{ 0 }; ///< 다른 바이트의 수
            uint8  _bAttribute{ SW_FALSE };  ///< 속성 구간(노멀 · 탄젠트 · UV · 색)이면 참, 위치 구간이면 거짓
        };

        /** @brief 정점 @p vertexCount 개(@p format)의 두 사본을 견줍니다. 같으면 nullopt 입니다. */
        static optional<Difference> findDifference( RHIMeshVertexFormat format, uint32 vertexCount, const uint8* pExpected, const uint8* pActual );

        /** @brief 도착한 사본을 견주어 로그로 남기고, 요청이 있으면 지금 장면의 메시를 되읽는 복사를 @p cmd 에 넣습니다. 프레임 업로드 단계 바로 뒤(렌더 패스 밖)에 부른다. */
        void service( IRHICommandList& cmd, IRHIDevice& device, const GPUScene& scene, RenderDebugRegistry& registry );
        /** @brief 버퍼를 놓습니다(렌더러가 장치 자원을 놓을 때). 여러 번 불러도 된다. */
        void release( IRHIDevice* pDevice );

    private:
        /** @brief 되읽는 메시 하나입니다. 되읽는 버퍼에는 [프록시 사본][그리는 버퍼의 사본] 이 잇달아 든다. */
        struct CheckedMesh
        {
            string              _name;
            RHIMeshVertexFormat _format{ RHIMeshVertexFormat::Packed };
            uint32              _vertexCount{ 0 };
            uint32              _expectedOffset{ 0 }; ///< `_listExpected` 안 시작 — 되읽는 버퍼의 시작은 그 두 배다
            uint32              _byteCount{ 0 };      ///< 사본 하나의 바이트
            uint32              _batchIndex{ 0 };     ///< 이 메시의 배치 번호(복사를 기록하는 프레임 안에서만 쓴다)
            RHIBufferHandle     _proxyBuffer{ 0 };    ///< 렌더 프록시의 정점 버퍼(놓였으면 장치가 모르는 핸들이다)
        };

        /** @brief 지금 장면에서 견줄 메시와 기대값(CPU 정점의 GPU 모양)을 모읍니다. */
        void collectMeshes( const GPUScene& scene );
        /** @brief 모은 메시의 되읽기 복사를 기록합니다. 버퍼를 만들지 못하면 아무것도 기록하지 않고 끝납니다(이유는 만드는 쪽이 남긴다). */
        void recordCopies( IRHICommandList& cmd, IRHIDevice& device, const GPUScene& scene );
        /** @brief 도착한 사본 @p listByte 를 기대값과 견주어 로그로 남깁니다. */
        void report( const IRHIDevice& device, const vector<uint8>& listByte ) const;

    private:
        RHIStructuredBuffer _probeBuffer{};
        AsyncBufferReadback _readback{};
        vector<CheckedMesh> _listMesh{};
        vector<uint8>       _listExpected{};       ///< CPU 정점의 GPU 모양(메시마다 잇달아)
        uint64              _tag{ 0 };             ///< 마지막 되읽기 요청의 tag
        uint8               _bPending{ SW_FALSE }; ///< 되읽기가 아직 도착하지 않았다
    };
} // namespace sw
