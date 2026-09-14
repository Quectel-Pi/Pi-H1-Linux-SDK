#!/bin/sh

gpioPins="36 37 77 50 51 16 101 17 18 32 33 41 40 19 42 43 80 52 53 49 48 76 78 103 34 35 104 102"
#gpioCount=${#gpioPins[@]}

#if [ ! -f /var/lock/gpio_config_done ]; then
#    echo "开始检查配置..."
#    
#    cp /etc/quecpi_config/quecpi_config.ini /etc/quecpi_config/quecpi_config.backup
#    if [ $? -ne 0 ]; then
#        echo "备份配置文件失败，请检查权限或路径。"
#        exit 1
#    fi
#
#    I2C9=$(grep -E "^i2c9=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#    SPI10=$(grep -E "^spi10=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#    UART12_2W=$(grep -E "^uart12_2w=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#    I2C13=$(grep -E "^i2c13=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#    SPI14=$(grep -E "^spi14=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#
#    target_i2c9="disable"
#    target_spi10="disable"
#    target_uart12_2w="disable"
#    target_i2c13="disable"
#    target_spi14="disable"
#
#    if [ "$I2C9" != "$target_i2c9" ] || [ "$SPI10" != "$target_spi10" ] || [ "$UART12_2W" != "$target_uart12_2w" ] || [ "$I2C13" != "$target_i2c13" ] || [ "$SPI14" != "$target_spi14" ]; then
#        echo "配置需要修改..."
#        
#        sed -i 's/^\(i2c9=\).*/\1'$target_i2c9'/' /etc/quecpi_config/quecpi_config.ini
#        sed -i 's/^\(spi10=\).*/\1'$target_spi10'/' /etc/quecpi_config/quecpi_config.ini
#        sed -i 's/^\(uart12_2w=\).*/\1'$target_uart12_2w'/' /etc/quecpi_config/quecpi_config.ini
#        sed -i 's/^\(i2c13=\).*/\1'$target_i2c13'/' /etc/quecpi_config/quecpi_config.ini
#        sed -i 's/^\(spi14=\).*/\1'$target_spi14'/' /etc/quecpi_config/quecpi_config.ini
#
#
#        new_I2C9=$(grep -E "^i2c9=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#        new_SPI10=$(grep -E "^spi10=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#        new_UART12_2W=$(grep -E "^uart12_2w=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#        new_I2C13=$(grep -E "^i2c13=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#        new_SPI14=$(grep -E "^spi14=" /etc/quecpi_config/quecpi_config.ini | cut -d'=' -f2)
#
#        if [ "$new_I2C9" != "$target_i2c9" ] || [ "$new_SPI10" != "$target_spi10" ] || [ "$new_UART12_2W" != "$target_uart12_2w" ] || [ "$new_I2C13" != "$target_i2c13" ] || [ "$new_SPI14" != "$target_spi14" ]; then
#            echo "配置修改失败，请检查权限或配置文件。"
#            # 恢复配置文件
#            cp /etc/quecpi_config/quecpi_config.backup /etc/quecpi_config/quecpi_config.ini
#            exit 1
#        else
#            echo "配置修改成功。"
#        fi
#
#        # 标记配置已完成
#        touch /var/lock/gpio_config_done
#        if [ $? -ne 0 ]; then
#            echo "创建配置完成标记文件失败。"
#            exit 1
#        fi
#
#        # 执行设置命令
#        quecpi_config 40pin set
#        if [ $? -ne 0 ]; then
#            echo "执行quecpi_config命令失败，请检查命令是否存在或可执行。"
#            exit 1
#        fi
#
#        # 重启系统
#        reboot
#    else
#        echo "配置已正确设置，无需修改。"
#        # 标记配置已完成
#        touch /var/lock/gpio_config_done
#        if [ $? -ne 0 ]; then
#            echo "创建配置完成标记文件失败。"
#            exit 1
#        fi
#    fi
#else
#    echo "配置已完成，跳过配置修改步骤。"
#fi

## 检查参数
#if [ "$1" != "on" ] && [ "$1" != "off" ]; then
#    echo "Usage: $0 on|off"
#    exit 1
#fi

# 执行LED灯控制
#if [ "$1" = "on" ]; then
    echo "点亮所有LED灯..."

    # 设置GPIO47为输出并置0（NO输出）
    rgs c 999 go 4
    rgs c 999 gso 0 47
    rgs c 999 gw 0 47 0
    if [ $? -ne 0 ]; then
        echo "设置GPIO47失败。"
        exit 1
    fi

    # 遍历gpioPins数组，设置每个引脚为输出并置1
    for i in $gpioPins; do
        rgs c 999 gso 0 $i
        if [ $? -ne 0 ]; then
            echo "设置GPIO $i 输出失败,继续处理其他GPIO。"
            continue
        fi
        rgs c 999 gw 0 $i 1
        if [ $? -ne 0 ]; then
            echo "设置GPIO $i 高电平失败,继续处理其他GPIO。"
        fi
    done
#elif [ "$1" = "off" ]; then
#    echo "熄灭所有LED灯..."
#
#    # 设置GPIO47为输出并置1（NC输出）
#    rgs c 999 go 4
#    rgs c 999 gso 0 47
#    rgs c 999 gw 0 47 1
#    if [ $? -ne 0 ]; then
#        echo "设置GPIO47失败。"
#        exit 1
#    fi
#
#    # 设置GPIO62和63为输出并置0
#    rgs c 999 gso 0 62
#    if [ $? -ne 0 ]; then
#        echo "设置GPIO62 输出失败。"
#        exit 1
#    fi
#    rgs c 999 gw 0 62 0
#    if [ $? -ne 0 ]; then
#        echo "设置GPIO62 低电平失败。"
#        exit 1
#    fi
#
#    rgs c 999 gso 0 63
#    if [ $? -ne 0 ]; then
#        echo "设置GPIO63 输出失败。"
#        exit 1
#    fi
#    rgs c 999 gw 0 63 0
#    if [ $? -ne 0 ]; then
#        echo "设置GPIO63 低电平失败。"
#        exit 1
#    fi
#
#    # 遍历gpioPins数组，设置每个引脚为输出并置0
#    for i in "${gpioPins[@]}"; do
#        rgs c 999 gso 0 $i
#        if [ $? -ne 0 ]; then
#            echo "设置GPIO $i 输出失败。"
#            exit 1
#        fi
#        rgs c 999 gw 0 $i 0
#        if [ $? -ne 0 ]; then
#            echo "设置GPIO $i 低电平失败。"
#            exit 1
#        fi
#    done
#fi

echo "操作完成。"
exit 0
