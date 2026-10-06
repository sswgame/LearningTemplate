/**
 * @file FileLocalSlotStorage.h
 * @brief 파일 로컬 저장 바닥 — 슬롯 `save/slot0` → `<루트>/save/slot0.swls`. 쓰기는 `FileUtil::writeFile`(같은 폴더 임시 파일 → 이름 바꾸기 — 옛 것 아니면 새 것).
 * @details 띄울 때 남은 임시 파일(`*.swls.tmp*` — 꺼진 쓰기의 찌꺼기)을 지운다. fsync 는 하지 않는다(언리얼 `SaveArrayToFile` 과 같다) — 전원이 나가 이름 바꾸기만
 *          남은 반쪽 파일은 봉투 체크섬이 Corrupt 로 잡는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"
#include "GameFramework/GameFrameworkExports.h"

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

        /** @brief @p rootDirectory(절대 경로 — `UserDataPath::resolve`)를 만들고 남은 임시 파일을 지웁니다. */
        explicit FileLocalSlotStorage( string_view rootDirectory );

        LocalStoreResult readSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
        LocalStoreResult writeSlot( const string& slot, const vector<uint8>& envelopeBytes ) override;
        LocalStoreResult eraseSlot( const string& slot ) override;
        LocalStoreResult listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo ) override;

        string makeSlotPath( const string& slot ) const;

    private:
        string _rootDirectory;
    };
} // namespace sw
