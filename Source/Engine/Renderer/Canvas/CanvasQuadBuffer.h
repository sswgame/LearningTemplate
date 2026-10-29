/**
 * @file CanvasQuadBuffer.h
 * @brief 캔버스 사각형 구조 버퍼입니다(렌더 스레드). 앞에 화면 앵커, 그 뒤에 주 출력, 그 뒤에 렌더 텍스처 대상들의 사각형을 한 버퍼에 잇습니다.
 * @details `CanvasRenderer` 가 값으로 소유합니다. 같은 프레임에 같은 버퍼를 두 번 갱신하지 않으려고 모든 목록을 한 번에 올립니다(언리얼 Slate 의 `FSlateElementBatcher` 정점 버퍼 자리).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"

namespace sw
{
    struct CanvasAnchor;
    struct CanvasFrameData;
    struct CanvasQuad;

    class IRHIDevice;

    /**
     * @class CanvasQuadBuffer
     * @brief 캔버스 사각형 버퍼와 그 안의 목록별 시작 위치입니다. **프레임에 한 번** 갱신하고, 내용 번호(`CanvasFrameData::_contentRevision`)가 지난번과 같으면 사각형 갱신을 건너뜁니다.
     */
    class SW_API CanvasQuadBuffer
    {
    public:
        CanvasQuadBuffer();
        ~CanvasQuadBuffer();

        CanvasQuadBuffer( const CanvasQuadBuffer& )            = delete;
        CanvasQuadBuffer& operator=( const CanvasQuadBuffer& ) = delete;

        /**
         * @brief 앵커 · 주 출력 · 대상 사각형을 이어 올립니다. 내용 서명(주 출력 · 대상의 내용 번호)과 사각형 수가 같고 버퍼가 그대로면 바뀐 앵커만 올립니다.
         * @param bNeedsUAV 이번 프레임에 마커 투영 컴퓨트가 앵커 원소를 쓴다(버퍼가 UAV 를 가져야 한다 — 없이 만든 버퍼는 다시 만들고 내용을 다시 올린다).
         */
        void upload( IRHIDevice& device, const CanvasFrameData& frame, bool bNeedsUAV );
        /** @brief 버퍼를 해제하고(디바이스가 없으면 핸들만 잊습니다) 올린 내용의 기억을 지웁니다. */
        void release( IRHIDevice* pDevice );

        /** @brief 사각형 구조 버퍼입니다(SRV 는 캔버스 그리기, UAV 는 마커 투영이 씁니다). */
        const RHIStructuredBufferSlot& getSlot() const { return _quadBuffer; }
        /** @brief 마지막 `upload` 에서 주 출력 사각형이 버퍼에서 시작하는 위치입니다(버퍼 맨 앞은 화면 앵커입니다). */
        uint32 getMainQuadBase() const { return _mainQuadBase; }
        /** @brief 마지막 `upload` 에서 대상 @p targetIndex 의 사각형이 버퍼에서 시작하는 위치입니다. */
        uint32 getTargetQuadBase( uint32 targetIndex ) const { return targetIndex < _listTargetQuadBase.size() ? _listTargetQuadBase[targetIndex] : 0; }

    private:
        /** @brief 화면 앵커를 버퍼 맨 앞에 업로드합니다. @p bForce 가 아니면 지난번과 같을 때 업로드하지 않습니다. */
        void uploadAnchors( IRHIDevice& device, const vector<CanvasAnchor>& listAnchor, bool bForce );

        vector<CanvasQuad>      _listAnchorScratch;  ///< 앵커를 사각형 원소 형태(rect = x, y, 배율, -)로 옮긴 사본
        vector<CanvasAnchor>    _listUploadedAnchor; ///< 버퍼 앞에 지금 있는 앵커(같으면 다시 업로드하지 않는다)
        vector<CanvasQuad>      _listQuadScratch;    ///< 대상이 있을 때 주 출력과 대상 사각형을 이어 붙인 사본
        vector<uint32>          _listTargetQuadBase; ///< 대상마다 버퍼 안 시작 위치
        RHIStructuredBufferSlot _quadBuffer;
        uint64                  _uploadedRevision; ///< 사각형 버퍼에 지금 있는 내용의 서명(주 출력과 대상 내용 번호, 0이면 없음)
        uint32                  _uploadedQuadCount;
        uint32                  _mainQuadBase; ///< 주 출력 사각형이 버퍼에서 시작하는 위치(앞의 앵커 수)
    };
} // namespace sw
