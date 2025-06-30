#!/usr/bin/python
# -*- coding: UTF-8 -*-
from __future__ import print_function
import os
import re
import sys
import time
import shutil
import glob
from pyhocon import ConfigFactory

env_list = os.environ
WS = env_list['TOPDIR']
conf = ConfigFactory.parse_file(os.path.join(WS,'quectel_build/config/quectel_project.conf'))

gen_prj_file=os.path.join(WS,'quectel_build/compile/quectel-features-config/quectel-buildconfig-gen.h')
gen_ver_file=os.path.join(WS,'quectel_build/compile/quectel-features-config/quectel_var.inc')


GP=None # which group config to use
prj_list=[] # vaild project list
custoct_list=[] # valid custoct list
#print(conf)

ql_env_dic = {'QUECTEL_PROJECT_NAME':'',
              'QUECTEL_PROJECT_REV':'',
              'QUECTEL_CUSTOM_NAME':'',
              'QUECTEL_FEATURE_OPENLINUX':'',
             }

opt_dic = {'.project_list':'Vaild Project List',
           '.flash':'Flash Information',
           '.function':'Function Switch',
           '.macro':'Add Macro Define',
          }

opt_dic_fp = {'.copy':'Copy Files Operate',
           '.symlink':'Symlink Files Operate',
           '.delete':'Delete Files Operate',
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
    prj_list_init(None)
    custoct_list_init(None)
    print ()
    print ("'ProjectRev' can be set any value, but should be same with modem")
    print ("'CustName' if this Version for Standard module, must be set 'STD'")
    print ("********************Build QSM565DWF Standard Firmware Demo*****************************")
    print ("\033[33mstep1: buildconfig QSM565DWF SG565DWFPARL1A01_BL01BP01K0M01_QDP_LP6.6.0XX.01.00X_V0X STD \033[0m")
    print ("\033[33mstep2: buildall \033[0m")
    print ("\033[33mstep3: buildpackage\033[0m")
    print ()
    print ("Vaild Projects:")
    print (color.FGREEN)
    for i in prj_list:
        print (i, end=' '),
    print (color.END)
    print ()
    print ("Vaild CUST_NAME:")
    print (color.FGREEN)
    for i in custoct_list:
        print (i, end=' '),
    print (color.END)
    print ()

def val_replace(content):
    global ql_env_dic
    content = content.replace('\"','')
    for k,v in ql_env_dic.items():
        if content.find('${'+k+'}'):
            if v:
                content = content.replace('${'+k+'}',v)
            else:
                content = content.replace('${'+k+'}','???')
    return content
    #print(content)

def prj_list_init(project):
    global GP
    for group in conf:
        tmp_prjs = conf.get(group+'.project_list')
        for prj in tmp_prjs:
            if project == prj:
                GP = group
            if prj in prj_list:
               continue
            prj_list.append(prj)
    #print prj_list

def prj_valid_check(project):
    prj_list_init(project)
    if project in prj_list:
        print (color.FGREEN+"Target porject hit"+color.END)
        #sys.exit(0)
    else:
        print ()
        print (color.FRED+'ERROR: invaild project!!!'+color.END)
        build_help(project)
        print (color.BLINK+color.FRED+color.BOLD+"Please Enter valid ProjectName(using 'buildconfig' get help)."+color.END)
        sys.exit(1)

def custoct_list_init(custoct):
    global GP
    for group in conf:
        tmp_cocts = conf.get(group+'.custoct_list')
        for coct in tmp_cocts:
            if coct == custoct:
                GP = group
            if coct in custoct_list:
               continue
            custoct_list.append(coct)

def custoct_valid_check(custoct):
    custoct_list_init(None)

    tmp_custoct = re.split('/', custoct)
    for target in tmp_custoct:
        if target in custoct_list:
            #sys.exit(0)
            continue
        else:
            print ()
            print (color.FRED+'ERROR: invaild custoct!!!'+color.END)
            build_help()
            print (color.BLINK+color.FRED+color.BOLD+"Please Enter valid CUST_NAME(using 'buildconfig' get help)."+color.END)
            sys.exit(1)

def custoct_hit_target(target):
    custoct = ql_env_dic.get("QUECTEL_CUSTOM_NAME")
    tmp_custoct = re.split('/', custoct)
    if target in tmp_custoct:
            return True
    return False

def display_option(opttype, subtype):
    global GP
    if opttype:
        opt_dictype = opt_dic_fp
    else:
        opt_dictype = opt_dic

    print ('{:=<83}'.format(' '))
    # display copy file list
    for opt,disc in opt_dictype.items(): 
        cmd_dic = ""
        if subtype == "default":
            cmd_dic = conf.get(GP + '.'+ opttype + opt)
        else:
            cmd_dic = conf.get(GP + '.'+ opttype + '.' + subtype + opt)
        if not cmd_dic: # for empty config
            continue
        print (' |','{:-<78}'.format('-'),'|')
        print ('{0}{1:^87}{0}'.format(' |', color.BOLD+disc+color.END))
        print (' |','{:-<78}'.format('-'),'|')
        if isinstance(cmd_dic,list): #for project list
            for i in cmd_dic:
                print (' | ','{:<76}'.format(i),' | ')
        else:
            for k,v in cmd_dic.items():
                k = val_replace(k)
                if isinstance(v,list): #for multiple dst file operate
                    for dst in v:
                        if isinstance(dst,str):
                            dst = val_replace(dst)
                        print ('{0}{2:<78}{0}'.format(' | ',' -> ', k))
                        print ('{0}{1}{2:>74}{0}'.format(' | ',' -> ', dst))
                else:
                    if isinstance(v,str):
                        v = val_replace(v)
                    if len(str(k)+str(v)) > 80:
                        print ('{0}{2:<78}{0}'.format(' | ',' -> ', k))
                        print ('{0}{1}{2:>74}{0}'.format(' | ',' -> ', v))
                    else:
                        print ('{0}{2:<25}{1}{3:<50}{0}'.format(' | ',' : ', k, v))
    print ('{:=<83}'.format(" "))


def display_config_file(project):
    global GP
    prj_list_init(project)
    display_option("", "");
    display_option("common", "default");

    custoct_current = re.split('/', ql_env_dic.get("QUECTEL_CUSTOM_NAME"))
    for target in custoct_current:
        if target in custoct_list:
            display_option("custom", target);

def ql_file_operate(src, dst, opt):
    dst = val_replace(dst)

    if opt == 'git':
        print ('copy: /usr/bin/git checkout -f '+dst)
        os.system('/usr/bin/git checkout -f '+dst)
        return True

    if opt == 'delete':
        print ('delete: rm '+dst)
        for path_name in glob.glob(dst):
            if not os.path.exists(path_name):
                print (color.FRED+"Waring: The file or directory "+dst+" not exist! Not need delete."+color.END)
                return True
            if os.path.isfile(path_name):
                os.system('rm '+path_name)
            if os.path.isdir(path_name):
                os.system('rm -rf '+path_name)
        return True

    src = val_replace(src)
    #if not os.path.exists(src):
    #    print color.FRED+"ERROR: Cont't find "+src+", no such file or dictionary!"+color.END
    #    return False

    dst_dir = os.path.dirname(dst)
    if not os.path.exists(dst_dir):
        os.system('mkdir -p '+dst_dir)

    if opt == 'symlink':
        print ('symlink: ln -sf '+src+' '+dst)
        os.system('ln -sf '+src+' '+dst)
        return True

    if opt == 'copy':
        print ('copy: cp '+src+' '+dst)
        if glob.glob(src):
            #print 'file exist'
            #shutil.copy(os.path.join(WS,cp_src),os.path.join(WS, cp_dst))
            os.system('cp -f '+src+' '+dst)
        if os.path.isdir(src):
            if os.path.exists(dst):
                #print 'dir exist'
                #shutil.copytree(os.path.join(WS,cp_src),os.path.join(WS, cp_dst))
                os.system('cp -rf '+src+'/* '+dst)
            else:
                #print 'dir not exist'
                os.system('cp -rf '+src+' '+dst)
        return True


def handle_file_operate(project,   custom):
    global GP
    #prj_list_init(project)
    opt_list = ['delete', 'git', 'copy', 'symlink']
    for opt in opt_list:
        if custom:
            file_dic = conf.get(GP+'.'+custom+'.'+opt)
        else:
            file_dic = conf.get(GP+'.'+opt)

        if opt == 'delete':
            if isinstance(file_dic,list):
                for dst in file_dic:
                    dst = os.path.join(WS, dst)
                    ql_file_operate(None, dst, opt)
        else:
            for k,v in file_dic.items():
                src = os.path.join(WS, k)
                #print src
                if isinstance(v,list):
                    for dst in v:
                        dst = os.path.join(WS, dst)
                        #print cp_dst
                        ql_file_operate(src, dst, opt)
                else:
                    dst = os.path.join(WS, v)
                    ql_file_operate(src, dst, opt)
                    #print cp_dst

def handle_hwrev_operate(hwrev):
    global GP

    #prj_list_init(project)
    opt = hwrev
    print ("[split hwrev file: "+hwrev+" ]")

    if re.match('HV', opt):
        file_dic = conf.get(GP+'.'+opt)
        for k,v in file_dic.items():
            src = os.path.join(WS, k)
            #print src
            if isinstance(v,list):
                for dst in v:
                    dst = os.path.join(WS, dst)
                    #print cp_dst
                    ql_file_operate(src, dst, 'copy')
            else:
                dst = os.path.join(WS, v)
                ql_file_operate(src, dst, 'copy')
                #print cp_dst

def handle_flash_operate(project):
    global g_flashconfig_list
    
    g_flashconfig_list = conf.get(GP+'.flash')
    for file_dic in g_flashconfig_list:
        ishit = custoct_hit_target(file_dic)
        if ishit == True:
            print ("[split flash file: "+file_dic+" ]")
            cur_flashconfig = conf.get(GP+'.flash.'+file_dic)
            for k,v in cur_flashconfig.items():
                src = os.path.join(WS, k)
                #print src
                if isinstance(v,list):
                    for dst in v:
                        dst = os.path.join(WS, dst)
                        #print cp_dst
                        ql_file_operate(src, dst, 'copy')
                else:
                    dst = os.path.join(WS, v)
                    ql_file_operate(src, dst, 'copy')
                    #print cp_dst

def handle_custom_operate(project, custom):
    global GP
    global custom_list

    custom_list = conf.get(GP+'.'+custom)
    for custom_dic in custom_list:
        ishit = custoct_hit_target(custom_dic)
        if ishit == True:
            print ("[custom file: "+custom_dic+" ]")
            handle_file_operate(project , custom + '.' + custom_dic)


def handle_macro():
    global GP
    macro_dic = conf.get(GP+'.macro')
    if macro_dic:
        return macro_dic

def gen_config_file(ql_env_dic):
    with open(gen_prj_file, 'w') as f:
        tmp_data = "/** WARNING:generate by using buildconfig comand, don't directly edit this file!!! */\n"
        tmp_data += "\n"
        tmp_data += "#ifndef __QL_BUILDCONFIG_GEN_H__\n"
        tmp_data += "#define __QL_BUILDCONFIG_GEN_H__\n"
        tmp_data += "\n"
        tmp_data += "/************Quectel autogen version************/\n"
        tmp_data += "\n"
        tmp_data += "#define QUECTEL_BUILD_TIME      \""+time.strftime('%b %d %Y %H:%M:%S',time.localtime(time.time()))+"\"\n"
        tmp_data += "#define QUECTEL_PROJECT_NAME    \""+ql_env_dic.get("QUECTEL_PROJECT_NAME")+"\"\n"
        tmp_data += "#define QUECTEL_PROJECT_REV     \""+ql_env_dic.get("QUECTEL_PROJECT_REV")+"\"\n"
        tmp_data += "#define QUECTEL_CUSTOM_NAME     \""+ql_env_dic.get("QUECTEL_CUSTOM_NAME")+"\"\n"
        if ql_env_dic['QUECTEL_FEATURE_OPENLINUX'] == 'OL':
            tmp_data += "#define QUECTEL_FEATURE_OPENLINUX \n"

        tmp_data += "/************Quectel Macro************/\n"
        tmp_data += "\n"
        if ql_env_dic.get("QUECTEL_PROJECT_NAME").find('_') == -1:
            tmp_data += "#define __QUECTEL_PROJECT_"+ql_env_dic.get("QUECTEL_PROJECT_NAME")+"__     1\n"
        else:
            tmp_data += "#define __QUECTEL_PROJECT_"+ql_env_dic.get("QUECTEL_PROJECT_NAME").replace("_","",1)+"__     1\n"
            tmp_data += "#define __QUECTEL_PROJECT_"+ql_env_dic.get("QUECTEL_PROJECT_NAME")+"__    1\n"

        tmp_data += "#define __QUECTEL_PROJECT_"+ql_env_dic.get("QUECTEL_PROJECT_NAME")[0:6]+"__    1\n"
        tmp_data += "\n"
        tmp_data += "/************Legacy Macro************/\n"
        tmp_data += "\n"
        tmp_data += "#define QL_G_PRODUCT_"+ql_env_dic.get("QUECTEL_PROJECT_NAME")+"    1\n"
            
        if ql_env_dic['QUECTEL_FEATURE_OPENLINUX'] == 'OL':
            tmp_data += "#define QL_G_OPENLINUX_MODE 1\n"
        print ("[split "+ql_env_dic.get("QUECTEL_CUSTOM_NAME")+"]")
        tmp_split = re.split('/', ql_env_dic.get("QUECTEL_CUSTOM_NAME"))
        for custoct in tmp_split:
            tmp_data += "#define QL_G_CUSTOCT_"+custoct+"    1\n"

        macro_dic = handle_macro()
        if macro_dic:
            for k,v in macro_dic.items():
                tmp_data += "#define "+k+" "+str(v)+"\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "#endif//#ifndef __QL_BUILDCONFIG_GEN_H__\n"
        f.write(tmp_data)
    f.close()


def gen_ol_prj_header_file(ql_env_dic):
    with open(gen_ol_prj_file, 'w') as f:
        tmp_data = "/** WARNING:generate by using buildconfig comand, don't directly edit this file!!! */\n"
        tmp_data += "\n"
        tmp_data += "#ifndef __QL_OL_PRJ_GEN_H__\n"
        tmp_data += "#define __QL_OL_PRJ_GEN_H__\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "/************Quectel autogen version************/\n"
        tmp_data += "\n"
        tmp_data += "\n"
        if ql_env_dic.get("QUECTEL_PROJECT_NAME").find('_') == -1:
            tmp_data += "#define __QUECTEL_PROJECT_"+ql_env_dic.get("QUECTEL_PROJECT_NAME")+"__     1\n"
        else:
            tmp_data += "#define __QUECTEL_PROJECT_"+ql_env_dic.get("QUECTEL_PROJECT_NAME").replace("_","",1)+"__     1\n"

        tmp_data += "#define __QUECTEL_PROJECT_"+ql_env_dic.get("QUECTEL_PROJECT_NAME")[0:6]+"__    1\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "\n"
        tmp_data += "#endif//#ifndef __QL_OL_PRJ_GEN_H__\n"
        f.write(tmp_data)
    f.close()

#Generate quectel var file for compile stage
def gen_var_file(ql_env_dic):
    with open(gen_ver_file, 'w') as f:
        tmp_data  = "QUECTEL_PROJECT_NAME = "+ql_env_dic.get("QUECTEL_PROJECT_NAME")+"\n"
        tmp_data += "QUECTEL_PROJECT_REV = "+ql_env_dic.get("QUECTEL_PROJECT_REV")+"\n"
        tmp_data += "QUECTEL_CUSTOM_NAME = "+ql_env_dic.get("QUECTEL_CUSTOM_NAME")+"\n"
        f.write(tmp_data)
    f.close()


def main(argv):
    print (argv)
    arg_len = len(argv)
    if arg_len < 4:
        print ("\033[31;1mPlease Enter 'ProjectName', 'ProjectRev', CustName.\033[0m")
        build_help()
        #sys.exit(1)
        return

    QUECTEL_PROJECT_NAME = argv[1]
    QUECTEL_PROJECT_REV = argv[2]
    QUECTEL_CUSTOM_NAME = argv[3]
    QUECTEL_PROJECT_HWREV = ""

    global ql_env_dic
    ql_env_dic['QUECTEL_PROJECT_NAME']= QUECTEL_PROJECT_NAME
    ql_env_dic['QUECTEL_PROJECT_REV']= QUECTEL_PROJECT_REV
    ql_env_dic['QUECTEL_PROJECT_HWREV']= QUECTEL_PROJECT_HWREV
    ql_env_dic['QUECTEL_CUSTOM_NAME']= QUECTEL_CUSTOM_NAME
    # ql_env_dic['QUECTEL_PRODUCT_NAME']= QUECTEL_PROJECT_NAME[0:6]+'_'+QUECTEL_PROJECT_NAME[6:]

    tmp_split = re.split('/', ql_env_dic.get("QUECTEL_CUSTOM_NAME"))
    for custoct in tmp_split:
        if 'OL' in custoct:
            ql_env_dic['QUECTEL_FEATURE_OPENLINUX']='OL'
            print ("will include openlinux feature")

    prj_valid_check(argv[1])
    custoct_valid_check(QUECTEL_CUSTOM_NAME)
    gen_config_file(ql_env_dic)
    gen_var_file(ql_env_dic)
    display_config_file(QUECTEL_PROJECT_NAME)
    handle_file_operate(QUECTEL_PROJECT_NAME, "common")
    handle_custom_operate(QUECTEL_PROJECT_NAME, "custom")

if __name__ == '__main__':
    main(sys.argv)
