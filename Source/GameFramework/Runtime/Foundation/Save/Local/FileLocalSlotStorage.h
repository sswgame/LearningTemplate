/**
 * @file FileLocalSlotStorage.h
 * @brief 파일 로컬 저장 백엔드입니다. 슬롯 `save/slot0` 은 `<루트>/save/slot0.swls` 파일이 됩니다.
 *        쓰기는 `FileUtil::writeFileDurable`(같은 폴더 임시 파일에 쓰고 fsync 뒤 이름 바꾸기)이라 전원이 나가도 결과는 옛 것이거나 새 것입니다.
 * @details 시작할 때 남은 임시 파일(`*.swls.tmp*`, 중단된 쓰기의 잔여물)을 지운다. 그래도 깨진 파일(장치 결함)은 봉투 체크섬이 Corrupt 로 잡고,
 *          쓰기마다 남긴 직전 세대(`<슬롯>.swls.prev`)로 돌아가 읽는다(`readPreviousSlot`).
 *          직전 세대는 바꿔 끼우기 전에 지금 파일을 복사해 둔다. 직전 세대는 fsync 하지 않는다 — 지금 파일이 이미 Durable 로 쓰였다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Save/Local/LocalSlotStorage.h"

namespace sw
{
    /**
     * @class FileLocalSlotStorage
     * @brief 파일 바닥입니다(`ThreadedLocalStore` 의 스레드에서만 불린다).
     */
    class SW_GF_API FileLocalSlotStorage final : public ILocalSlotStorage
    {
    public:
        static constexpr const utf8* kSlotExtension = ".swls";
        /** @brief 직전 세대 파일의 뒷이름입니다(`<슬롯>.swls.prev`). 나열(`listSlots`)은 확장자가 달라 보지 않는다. */
        static constexpr const utf8* kPreviousGenerationSuffix = ".prev";

        /** @brief @p rootDirectory(`LocalStoreLocation::resolveDiskPath` 가 낸 경로)를 만들고 남은 임시 파일을 지웁니다. */
        explicit FileLocalSlotStorage( const AbsolutePath& rootDirectory );

        LocalStoreResult readSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
        LocalStoreResult readPreviousSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
        LocalStoreResult writeSlot( const string& slot, const vector<uint8>& envelopeBytes ) override;
        LocalStoreResult eraseSlot( const string& slot ) override;
        LocalStoreResult listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo ) override;

        AbsolutePath makeSlotPath( const string& slot ) const;

    private:
        AbsolutePath _rootDirectory;
    };
} // namespace sw
