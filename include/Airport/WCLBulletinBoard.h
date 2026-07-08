//
//  WCLBulletinBoard.h
//  itlwm
//
//  Forward declarations for the Tahoe WCL bulletin terminal path.
//

#ifndef WCLBulletinBoard_h
#define WCLBulletinBoard_h

#include <stdint.h>

enum WCLBulletinBoardManagerId : int {
    kWCLBulletinBoardManagerDriver = 13,
};

enum WCLBulletinBoardMsgKind : uint32_t {
    kWCLBulletinBoardMsgKindDriverEvent = 2,
};

// 0x30-byte WCL message object. WCLGlue::receiveMessageInternal builds
// msgWord0 as (eventId << 16) | kind, then copies len/payload at +0x8/+0x10.
struct bulletinBoardMessage {
    uint32_t msgWord0;
    uint32_t _pad0;
    uint64_t size;
    void    *payload;
    uint64_t _r18;
    uint64_t _r20;
    uint8_t  _r28;
    uint8_t  _pad1[7];
};

class WCLBulletinBoard
{
public:
    int sendMessage(WCLBulletinBoardManagerId, bulletinBoardMessage &);
};

#endif /* WCLBulletinBoard_h */
