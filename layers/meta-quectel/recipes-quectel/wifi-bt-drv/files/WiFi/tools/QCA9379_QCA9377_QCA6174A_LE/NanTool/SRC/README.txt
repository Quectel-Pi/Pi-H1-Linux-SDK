Steps to build the Nan Demo Tool
1) Make sure libnl-3-dev and libbsd-dev packages
 are installed in the pc

2) Build the shared LIB (libwifi-hal-qcom.so)
       
     cd LIB
       
     make

3) Export the shared lib path by doing
     
    export LD_LIBRARY_PATH=<YOUR_NAN_TOOLPATH>/SRC/LIB/
                    
4) Build the NanDemoTool
 
    cd ..

    make

NOTE: if the shared library path is not set in the shell, Nan Demo tool 
will not execute.
