#ifndef __LOG_H__
#define __LOG_H__

#define ALOGE(...) {printf("[ERR]");printf(__VA_ARGS__);printf("\n");}
#define ALOGV(...) {printf("[VERBOSE]");printf(__VA_ARGS__);printf("\n");}
#define ALOGI(...) {printf("[INFO]");printf(__VA_ARGS__);printf("\n");}
#define ALOGD(...) {printf("[DBG]");printf(__VA_ARGS__);printf("\n");}

#endif
