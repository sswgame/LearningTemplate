/**
 * @file FileLocalSlotStorage.h
 * @brief 파일 로컬 저장 백엔드입니다. 슬롯 `save/slot0` 은 `<루트>/save/slot0.swls` 파일이 됩니다.
 *        쓰기는 `FileUtil::writeFile`(같은 폴더 임시 파일에 쓰고 이름 바꾸기)이라 결과는 옛 것이거나 새 것입니다.
 * @details 시작할 때 남은 임시 파일(`*.swls.tmp*`, 중단된 쓰기의 잔여물)을 지운다. fsync 는 하지 않는다(언리얼 `SaveArrayToFile` 과 같다). 전원이 나가 이름 바꾸기만
 *          남은 반쪽 파일은 봉투 체크섬이 Corrupt 로 잡는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

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

        /** @brief @p rootDirectory(`LocalStoreLocation::resolveDiskPath` 가 낸 경로)를 만들고 남은 임시 파일을 지웁니다. */
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
