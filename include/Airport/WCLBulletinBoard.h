//
//  WCLBulletinBoard.h
//  itlwm
//
//  Forward declarations for the Tahoe WCL LQM bulletin terminal path.
//

#ifndef WCLBulletinBoard_h
#define WCLBulletinBoard_h

#include <stdint.h>

enum WCLBulletinBoardManagerId : int {
    kWCLBulletinBoardManagerNet = 4,
};

// 0x30-byte WCL message object. WCLNetManager::handleLqmUpdate reads
// msg+0x8 as payload size and msg+0x10 as the 0x1dc LQM payload.
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
    void sendMessage(WCLBulletinBoardManagerId, bulletinBoardMessage &);
    int routeMsg(WCLBulletinBoardManagerId, bulletinBoardMessage &,
                 unsigned int mask, void *, unsigned int);
};

class WCLNetManager
{
public:
    void handleLqmUpdate(bulletinBoardMessage &);
};

#endif /* WCLBulletinBoard_h */
