;------------------------------------loader 和 kernel-----------------------------
LOADER_BASE_ADDR equ 0x900      ;loader 的地址（内核加载器）
LOADER_START_SECTOR equ 0x2     ;读取的扇区，以0开始 LBA

;-------------- gdt描述符属性 --------------------------------
DESC_G_4K   equ     1_000_0000_0000_0000_0000_0000b   ;第23位G   1为4k
DESC_D_32   equ      1_00_0000_0000_0000_0000_0000b   ;第22位D/B 1为32为操作数 有效地址
DESC_L      equ       0_0_0000_0000_0000_0000_0000b   ;第21位L   0为不设置成64位代码段忽略
DESC_AVL    equ         0_0000_0000_0000_0000_0000b   ;第20位AVL 不设置操作系统额外提供软件可用的

DESC_LIMIT_CODE2     equ  1111_0000_0000_0000_0000b   ;第16-19位 段界限最后四位 全部为1 最大段界限*粒度(4k * 0xfffff)
DESC_LIMIT_DATA2     equ  DESC_LIMIT_CODE2            ;必须等于0xffff_ffff 数据段与代码段段界限相同
DESC_LIMIT_VIDEO2    equ  0000_0000_0000_0000_0000b   ;第16-19位 显存区描述符VIDEO2 低位即可表示段基址

DESC_P      equ               1_000_0000_0000_0000b	  ;第15位  P present判断段是否存在于内存  
DESC_DPL_0  equ                00_0_0000_0000_0000b   ;第13-14位 Privilege Level 0-3
DESC_DPL_1  equ                01_0_0000_0000_0000b	  ;0为操作系统权力最高 3为用户段用于保护
DESC_DPL_2  equ                10_0_0000_0000_0000b
DESC_DPL_3  equ                11_0_0000_0000_0000b

DESC_S_sys  equ                   0_0000_0000_0000b   ;第12位 为0表示系统段 为1表示数据段
DESC_S_CODE equ                   1_0000_0000_0000b   ;代码段为数据段的一种
DESC_S_DATA equ                         DESC_S_CODE


DESC_TYPE_CODE      equ             1000_0000_0000b   ;第9-11位 X=1 R=0 C=0 A=0 C1可执行 A访问位
DESC_TYPE_DATA      equ             0010_0000_0000b   ;第9-11位 X=0 E=0 W=1 A=0 E1下扩展

                                ;代码段描述符高四字节
DESC_CODE_HIGH4     equ         (0x00<<24) + DESC_G_4K + DESC_D_32 + \
                                DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + \
                                DESC_P + DESC_DPL_0 + DESC_S_CODE + \
                                DESC_TYPE_CODE + 0X00

                                ;数据段描述符高四字节
DESC_DATA_HIGH4     equ         (0x00<<24) + DESC_G_4K + DESC_D_32 + \
                                DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + \
                                DESC_P + DESC_DPL_0 + DESC_S_DATA + \
                                DESC_TYPE_DATA + 0X00

                                ;显存段描述符高4字节
DESC_VIDEO_HIGH4    equ         (0x00<<24) + DESC_G_4K + DESC_D_32 + \
                                DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + \
                                DESC_P + DESC_DPL_0 + DESC_S_DATA + \
                                DESC_TYPE_DATA + 0X0B ;文字的显存在0xb8000,这里是高16位，书中有误

;-------------------- 选择子属性 --------------------------------
;第0-1位 RPL 特权级比较是否允许访问  第2位TI 0表示GDT 1表示LDT    第3-15位索引值
RPL0    equ   00b
RPL1    equ   01b
RPL2    equ   10b
RPL3    equ   11b
TI_GDT  equ   000b
TI_LDT  equ   100b