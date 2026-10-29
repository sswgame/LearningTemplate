/**
 * @file Light2DCookieAtlas.h
 * @brief 2D 빛의 쿠키(빛 텍스처)를 텍스처 하나(아틀라스)의 칸에 모읍니다. 빛 받는 스프라이트는 패스 역할 텍스처 `Light2DCookieAtlas` 하나만 읽습니다.
 * @details 빛마다 텍스처를 바인딩하지 않는 이유: 에뮬 백엔드(DX11 · GL)는 패스 텍스처가 역할마다 한 칸(shaderslot::emulated)뿐이고 머티리얼 칸은 드로우의 머티리얼 것이다.
 *          아틀라스 하나면 네 백엔드가 같은 경로(역할 텍스처 + 칸 번호)로 읽는다. 칸은 `shaderslot::kLight2DCookieCellCount` 개이고, 칸 번호는 빛 원소의
 *          `_extra.w` 에 적는다(lighting2d.hlsli LightData::sampleLight2dCookie).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"

namespace sw
{
    struct GPULightCookie;

    class IRHIDevice;
    class Texture2D;

    /**
     * @class Light2DCookieAtlas
     * @brief 쿠키 텍스처 경로마다 아틀라스 칸 하나를 주고, 텍스처를 텍스처 캐시에서 빌려 둡니다(렌더 스레드).
     * @details 이번 프레임에 안 쓰인 쿠키는 칸과 텍스처를 돌려준다. 새로 칸을 받거나 텍스처가 다시 읽힌(핫 리로드 · 디바이스 교체) 칸은 그릴 목록에 오른다 —
     *          `FramePassExecutor::drawLight2DCookieAtlas` 가 그 칸에 텍스처를 옮겨 그린다. 칸이 모자라거나 텍스처를 못 읽으면 오류를 남기고 그 빛은 쿠키 없이 그린다
     *          (칸 번호를 적지 않는다 — 다른 칸의 쿠키가 비치지 않는다).
     */
    class SW_API Light2DCookieAtlas
    {
    public:
        /** @brief 그릴 칸 하나입니다. */
        struct CellDraw
        {
            uint32           _cell{ 0 };
            const Texture2D* _pTexture{ nullptr };
        };

        Light2DCookieAtlas();
        ~Light2DCookieAtlas();
        Light2DCookieAtlas( const Light2DCookieAtlas& )            = delete;
        Light2DCookieAtlas& operator=( const Light2DCookieAtlas& ) = delete;

        /**
         * @brief 이번 프레임의 쿠키에 칸을 나누고 @p inoutListLight 의 그 원소에 칸 번호(`_extra.w`)를 적습니다. 아틀라스 렌더 타깃이 없으면 만듭니다.
         * @param listCookie `collectSceneLights` 가 모은 쿠키 목록(원소 번호 · 경로)
         */
        void assignCells( IRHIDevice* pDevice, const vector<GPULightCookie>& listCookie, vector<GPULight>& inoutListLight );
        /** @brief 이번 프레임에 그릴 칸들입니다(새 칸 · 다시 읽힌 텍스처). `clearPendingDraws` 까지 남습니다. */
        const vector<CellDraw>& getPendingDraws() const { return _listPendingDraw; }
        void                    clearPendingDraws() { _listPendingDraw.clear(); }
        /** @brief 아틀라스 렌더 타깃입니다. 쿠키가 하나도 없거나 만들지 못했으면 nullptr 입니다. */
        const Texture2D* findAtlas() const;
        /** @brief 이번 프레임에 칸을 받은 쿠키가 있는지입니다 — 없으면 역할 텍스처를 걸지 않는다. */
        bool hasCookies() const { return _bHasCookies == SW_TRUE; }
        /** @brief 칸 @p cell 의 아틀라스 텍셀 사각형(x, y, 한 변)입니다. */
        static void computeCellRect( uint32 cell, uint32& outX, uint32& outY, uint32& outSize );
        /** @brief 빌린 텍스처를 모두 돌려주고 아틀라스를 놓습니다(디바이스 해제 · 종료). */
        void release( IRHIDevice* pDevice );

    private:
        struct Entry
        {
            hashed_string    _path{};
            const Texture2D* _pTexture{ nullptr };
            RHITextureHandle _drawnHandle{ 0 }; ///< 칸에 그린 때의 텍스처 핸들 — 다시 읽기 · 디바이스 교체로 바뀌면 다시 그린다
            uint32           _cell{ 0 };
            uint8            _bUsed{ SW_FALSE };
        };

        /** @brief 비어 있는 칸 번호입니다. 없으면 `kLight2DCookieCellCount` 입니다. */
        uint32 findFreeCell() const;

        vector<Entry>         _listEntry;
        vector<CellDraw>      _listPendingDraw;
        unique_ptr<Texture2D> _atlas;
        RHITextureHandle      _drawnAtlasHandle{ 0 }; ///< 칸을 그린 때의 아틀라스 핸들 — 디바이스 교체로 바뀌면(내용이 비었다) 모든 칸을 다시 그린다
        uint32                _reloadGeneration{ 0 }; ///< 텍스처 캐시의 다시 읽기 횟수 — 바뀌면 모든 칸을 다시 그린다
        uint8                 _bHasCookies{ SW_FALSE };
        uint8                 _bOverflowReported{ SW_FALSE };
    };
} // namespace sw
