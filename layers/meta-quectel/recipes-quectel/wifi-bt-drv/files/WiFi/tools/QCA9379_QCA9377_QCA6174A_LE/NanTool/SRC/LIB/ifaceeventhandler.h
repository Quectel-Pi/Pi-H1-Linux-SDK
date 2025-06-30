/* 
 *Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#ifndef __WIFI_HAL_IFACEEVENTHANDLER_COMMAND_H__
#define __WIFI_HAL_IFACEEVENTHANDLER_COMMAND_H__

#include "common.h"
#include "cpp_bindings.h"
#ifdef __GNUC__
#define PRINTF_FORMAT(a,b) __attribute__ ((format (printf, (a), (b))))
#define STRUCT_PACKED __attribute__ ((packed))
#else
#define PRINTF_FORMAT(a,b)
#define STRUCT_PACKED
#endif
//#include "vendor_definitions.h"
#include "qca-vendor_copy.h"
#include "wifi_hal.h"

//#include "qca-vendor_copy.h"
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */


class wifiEventHandler: public WifiCommand
{
private:
    int mRequestId;

protected:
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    u32 mSubcmd;

public:
    wifiEventHandler(wifi_handle handle, int id, u32 subcmd);
    virtual ~wifiEventHandler();
    virtual int get_request_id();
    virtual int handleEvent(WifiEvent &event);
};

class IfaceEventHandlerCommand: public wifiEventHandler
{
private:
    char *mEventData;
    u32 mDataLen;
    wifi_event_handler mHandler;

public:
    IfaceEventHandlerCommand(wifi_handle handle, int id, u32 subcmd);
    virtual ~IfaceEventHandlerCommand();

    virtual int handleEvent(WifiEvent &event);
    virtual void setCallbackHandler(wifi_event_handler nHandler);
    virtual int get_request_id();
};
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif
