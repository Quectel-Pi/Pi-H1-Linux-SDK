#!/usr/lib/python
# -*- coding: UTF-8 -*-
from __future__ import print_function
import os
import re
import sys
import time
import shutil
from pyhocon import ConfigFactory

env_list = os.environ
WS = env_list['TOPDIR']
gen_ver_file=os.path.join(WS,'quectel_build/compile/quectel-features-config/quectel-buildversion-gen.h')


ql_env_dic = {'Project':'QSM565DWF',
              'Version':'SG565DWFPARL1A01_BL01BP01K0M01_QDP_LP6.6.0XX.01.00X_V0X',
             }

class color:
    FBLACK = '\033[30m'
    FRED = '\033[31m'
    FGREEN = '\033[32m'
    FYELLOW = '\033[33m'
    FBLUE = '\033[34m'
    FPURPLE = '\033[35m'
    FCYAN = '\033[36m'
    FWHITE = '\033[37m'

    BBLACK = '\033[40m'
    BRED = '\033[41m'
    BGREEN = '\033[42m'
    BYELLOW = '\033[43m'
    BBLUE = '\033[44m'
    BPURPLE = '\033[45m'
    BCYAN = '\033[46m'
    BWHITE = '\033[47m'

    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    BLINK = '\033[5m'
    INVERT = '\033[7m'
    END = '\033[0m'
    # e.g: print color.BOLD + 'Hello World !' + color.END

def build_help():
    print ()
    print ("'ProjectRev' can be set any value, but should be same with modem")
    print ("'CustName' if this Version for Standard module, must be set 'STD'")
    print ("********************Build QSM565DWF Standard Firmware Demo*****************************")
    print ("\033[33mstep1: buildconfig QSM565DWF  SG565DWFPARL1A01_BL01BP01K0M01_QDP_LP6.6.0XX.01.00X_V0X STD \033[0m")
    print ("\033[33mstep2: buildall \033[0m")
    print ("\033[33mstep3: buildpackage\033[0m")
    print ()
    sys.exit()

def gen_version_file(ql_env_dic):
    with open(gen_ver_file, 'w') as f:
        tmp_data = "/******************************************************************************\n"
        tmp_data += "*\n"
        tmp_data += "*     Copyright (c) 2018 Quectel Ltd.\n"
        tmp_data += "*        \n"
        tmp_data += "*******************************************************************************\n"
        tmp_data += "*  file name:          quectel-buildversion-gen.h\n"
        tmp_data += "*  author:             zenith.zhang\n"
        tmp_data += "*  date:               "+time.strftime('%Y-%m-%d,%H:%M:%S',time.localtime(time.time()))+"\n"
        tmp_data += "*  file description:   [quectel at version infomation, the file automatically update\n"
        tmp_data += "                        the version number based on the configuration ]\n"
        tmp_data += "*******************************************************************************\n"
        tmp_data += "      EDIT HISTORY FOR MODULE\n"
        tmp_data += "  WARNING:generate by using buildversion comand, don't directly edit this file!!!\n"
        tmp_data += "******************************************************************************/\n"
        tmp_data += "\n"
        tmp_data += "#ifndef __QL_BUILDVERSION_GEN_H__\n"
        tmp_data += "#define __QL_BUILDVERSION_GEN_H__ \n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "#ifdef __cplusplus\n"
        tmp_data += "#if __cplusplus\n"
        tmp_data += 'extern "C" {\n'
        tmp_data += "#endif\n"
        tmp_data += "#endif\n"
        tmp_data += "/************Quectel autogen version************/\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "#define QUECTEL_PROJECT_Project      \""+ql_env_dic.get("Project")+"\"\n"
        tmp_data += "#define QUECTEL_PROJECT_Version      \""+ql_env_dic.get("Version")+"\"\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "#ifdef __cplusplus\n"
        tmp_data += "#if __cplusplus\n"
        tmp_data += "    }\n"
        tmp_data += "#endif\n"
        tmp_data += "#endif\n"
        tmp_data += "\n"
        tmp_data += "#endif //#ifndef __QL_BUILDVERSION_GEN_H__\n"

        f.write(tmp_data)
    f.close()

def main(argv):
    print (argv)
    arg_len = len(argv)
    if arg_len < 3:
        print ("\033[31;1mPlease Enter 'Project', 'Version', .\033[0m")
        build_help()
        sys.exit(1)

    QUECTEL_PROJECT_NAME = argv[1]
    QUECTEL_PROJECT_REV = argv[2]
    QUECTEL_CUSTOM_NAME = argv[3]

    global ql_env_dic
    ql_env_dic['QUECTEL_PROJECT_NAME']= QUECTEL_PROJECT_NAME
    ql_env_dic['QUECTEL_PROJECT_REV']= QUECTEL_PROJECT_REV
    ql_env_dic['QUECTEL_CUSTOM_NAME']= QUECTEL_CUSTOM_NAME

    index = 0

    # <Project>
    ql_env_dic['Project'] = ql_env_dic['QUECTEL_PROJECT_NAME']

    # <Version>
    index = ql_env_dic['QUECTEL_PROJECT_REV'].find("BETA")
    if index != -1:
        tmpstr = ql_env_dic['QUECTEL_PROJECT_REV'][index:]
        if tmpstr.find('V') != -1 or index < ql_env_dic['QUECTEL_PROJECT_REV'].rfind('_'):
            index = ql_env_dic['QUECTEL_PROJECT_REV'].rfind('_')
            ql_env_dic['Version']=ql_env_dic['QUECTEL_PROJECT_REV'][0:index]
        else:
            ql_env_dic['Version']=ql_env_dic['QUECTEL_PROJECT_REV']
    else:
        index = ql_env_dic['QUECTEL_PROJECT_REV'].rfind('_')
        ql_env_dic['Version']=ql_env_dic['QUECTEL_PROJECT_REV'][0:index]

    print ("[")
    print ("Project===>"+ql_env_dic['Project'])
    print ("Version===>"+ql_env_dic['Version'])
    print ("]")

if __name__ == '__main__':
    main(sys.argv)
