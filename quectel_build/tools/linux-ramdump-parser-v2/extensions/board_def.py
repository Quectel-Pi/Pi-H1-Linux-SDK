# QCS6490 / QCM6490 / SC7280 platform board definitions for
# linux-ramdump-parser-v2.
#
# 关键地址来源: sources/quectel-src/kernel/qcom-6.6/arch/arm64/boot/dts/qcom/sc7280.dtsi
#   smem_mem: memory@80900000  reg = <0x0 0x80900000 0x0 0x200000>
# qcm6490-idp.dts:
#   qcom,msm-id = <497 ...>, <498 ...>, <475 ...>, <515 ...>
#   socid 475 = QCM6490, 497/498 = SC7280/SC7280P, 515 = QCM6490 rev2
# DDR 起始(见 dump_info.txt): 0x80000000

from boards import Board


class BoardQCS6490(Board):
    """QCS6490 / QCM6490 (sc7280 family), arm64, Linux 6.6."""

    def __init__(self):
        super(BoardQCS6490, self).__init__()
        self.socid = 475            # QCM6490; SMEM socinfo.id 校验用
        self.board_num = '6490'    # --force-hardware 值(字符串,与命令行一致)
        self.cpu = 'ARM64'          # aarch64
        self.ram_start = 0x80000000     # DDR CS0 物理起始
        self.imem_start = 0x14680000    # OCIMEM (见 dump_info.txt)
        self.smem_addr = 0x80900000     # SMEM region (sc7280.dtsi)
        self.phys_offset = 0x80000000  # kernel TEXT 的物理偏移(arm64 常等于 ram_start)
        self.wdog_addr = None           # arm64 FIQ 由其它机制，这里置 None 跳过
        self.imem_file_name = 'OCIMEM.BIN'


class BoardSC7280(Board):
    """SC7280 family (兼容 socid 497/498/515)."""

    def __init__(self):
        super(BoardSC7280, self).__init__()
        self.socid = 497
        self.board_num = 7280
        self.cpu = 'ARM64'
        self.ram_start = 0x80000000
        self.imem_start = 0x14680000
        self.smem_addr = 0x80900000
        self.phys_offset = 0x80000000
        self.wdog_addr = None
        self.imem_file_name = 'OCIMEM.BIN'


# 实例化以注册到 boards 列表（Board.__init__ 内会调 register_board）
BoardQCS6490()
BoardSC7280()
BoardQCS6490.__init__.__defaults__  # noqa: 仅触发类已加载
