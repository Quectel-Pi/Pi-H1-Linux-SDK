/* 
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#include "sync.h"
#define LOG_TAG  "WifiHAL"
#include <time.h>

#include "ifaceeventhandler.h"

/* Used to handle NL command events from driver/firmware. */
IfaceEventHandlerCommand *mwifiEventHandler = NULL;
/* This function will be the main handler for the registered incoming
 * (from driver) Commads. Calls the appropriate callback handler after
 * parsing the vendor data.
 */
int IfaceEventHandlerCommand::handleEvent(WifiEvent &event)
{
    wifiEventHandler::handleEvent(event);

    switch(mSubcmd)
    {
        case NL80211_CMD_REG_CHANGE:
        {
            char code[2];
            memset(&code[0], 0, 2);
            if(tb[NL80211_ATTR_REG_ALPHA2])
            {
                memcpy(&code[0], (char *) nla_data(tb[NL80211_ATTR_REG_ALPHA2]), 2);
            } else {
                printf("%s: NL80211_ATTR_REG_ALPHA2 not found", __func__);
            }
            printf("Country : %c%c", code[0], code[1]);
            if(mHandler.on_country_code_changed)
            {
                mHandler.on_country_code_changed(code);
            }
        }
        break;
        default:
            printf("NL Event : %d Not supported", mSubcmd);
    }

    return NL_SKIP;
}

IfaceEventHandlerCommand::IfaceEventHandlerCommand(wifi_handle handle, int id, u32 subcmd)
        : wifiEventHandler(handle, id, subcmd)
{
    printf("wifiEventHandler %p constructed", this);
    registerHandler(mSubcmd);
    memset(&mHandler, 0, sizeof(wifi_event_handler));
    mEventData = NULL;
    mDataLen = 0;
}

IfaceEventHandlerCommand::~IfaceEventHandlerCommand()
{
    printf("IfaceEventHandlerCommand %p destructor", this);
    unregisterHandler(mSubcmd);
}

void IfaceEventHandlerCommand::setCallbackHandler(wifi_event_handler nHandler)
{
    mHandler = nHandler;
}

int wifiEventHandler::get_request_id()
{
    return mRequestId;
}

int IfaceEventHandlerCommand::get_request_id()
{
    return wifiEventHandler::get_request_id();
}

wifiEventHandler::wifiEventHandler(wifi_handle handle, int id, u32 subcmd)
        : WifiCommand(handle, id)
{
    mRequestId = id;
    mSubcmd = subcmd;
    registerHandler(mSubcmd);
    printf("wifiEventHandler %p constructed", this);
}

wifiEventHandler::~wifiEventHandler()
{
    printf("wifiEventHandler %p destructor", this);
    unregisterHandler(mSubcmd);
}

int wifiEventHandler::handleEvent(WifiEvent &event)
{
    struct genlmsghdr *gnlh = event.header();
    mSubcmd = gnlh->cmd;
    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
            genlmsg_attrlen(gnlh, 0), NULL);
    printf("Got NL Event : %d from the Driver.", gnlh->cmd);

    return NL_SKIP;
}
